;############################################################################
;##                                                                        ##
;##  AILA.ASM                                                              ##
;##                                                                        ##
;##  Miles Sound System                                                    ##
;##                                                                        ##
;##  32-bit flat-model assembly API functions                              ##
;##                                                                        ##
;##  Version 3.00 of 11-Apr-94: Initial V3.X version (derived from V1.04)  ##
;##     Differences from V1.04: System timer hooked on startup/shutdown    ##
;##                             EOI always sent at start of API_timer      ##
;##                             Timer clients may be re-entered and        ##
;##                             should maintain semaphores and/or switch   ##
;##                             stacks if necessary                        ##
;##                             New functions added for V3.0 support       ##
;##          3.01 of  5-Oct-94: Added 16-bit driver ISR support            ##
;##          3.02 of  6-Nov-94: Added AIL_background() call as debug aid   ##
;##          3.03 of  9-Jun-95: Fixed reentry window issue with Win95      ##
;##                                                                        ##
;##  80386 ASM source compatible with Microsoft Assembler v6.0 or later    ##
;##  C function prototypes in AIL.H                                        ##
;##                                                                        ##
;##  Author: John Miles                                                    ##
;##                                                                        ##
;############################################################################
;##                                                                        ##
;##  Copyright (C) RAD Game Tools, Inc.                                    ##
;##                                                                        ##
;##  Contact RAD Game Tools at 425-893-4300 for technical support.         ##
;##                                                                        ##
;############################################################################

                OPTION SCOPED           ;Enable local labels
                .386                    ;Enable 386 instruction set

                IFDEF ZORTECH

                .MODEL SMALL,C          ;Small 32-bit memory model and C calls
DGROUP          group _TEXT,_DATA       ;(Zortech's BLINK needs this)

                ELSE

                .MODEL FLAT,C

                ENDIF

                ;
                ;Configuration equates
                ;

FALSE           equ 0
TRUE            equ -1

EARLY_EOI       equ TRUE                ;Set TRUE to enable other interrupts
                                        ;during timer tick processing --
                                        ;on slower systems, this may cause
                                        ;"Transfer Stack Overflow" faults

REENTRY_ALLOWED equ FALSE               ;Set TRUE to enable nested timer tick
                                        ;interrupts (no effect if EARLY_EOI
                                        ;disabled)

SWITCH_STACK    equ TRUE                ;Set TRUE to switch to internal 2K
                                        ;stack (no effect if REENTRY_ALLOWED
                                        ;enabled)

SHOW_MODE       equ FALSE               ;Set TRUE for real/protected ISR
                                        ;display on text screen (for diag
                                        ;purposes only; FALSE to ship!)

ISR_STACK_SIZE  equ 512                 ;Size of USE16 ISR stack

SAVE_FPU	equ TRUE                ;Save state of FPU on interrupt

                ;
                ;Macros, internal equates
                ;

                INCLUDE mssasm.inc      ;DOS extender macros

HOOK_INT        equ 8                   ;Timer tick interrupt to capture

N_TIMERS        equ 16                  ;# of available timer handles
BIOS_H          equ N_TIMERS-1          ;Handle to BIOS timer (last timer)

TIMER_FREE      equ 0                   ;timer_status[] values
TIMER_STOPPED   equ 1
TIMER_RUNNING   equ 2

                .DATA

                ;
                ;Start of locked data
                ;

data_start      LABEL BYTE

timer_callback  dd N_TIMERS dup (0)     ;Callback function addrs for timers
timer_status    dd N_TIMERS dup (0)     ;Status of timers (0=free 1=off 2=on)
timer_elapsed   dd N_TIMERS dup (0)     ;Modified DDA error counts for timers
timer_value     dd N_TIMERS dup (0)     ;Modified DDA limit values for timers
timer_trigger   dd N_TIMERS dup (0)     ;0 if timer not triggered, else n
timer_user      dd N_TIMERS dup (0)     ;User parameter for timers

timerticks      dd 0                    ;incremented every timer tick
lastmscount     dd 0                    ;last returned from ail_ms_count
adjstartms      dd 0                    ;adjusted amount for timer reprograms
adjstartus      dd 0                    ;adjusted amount for timer reprograms
lastapitimer    dd 0                    ;last value read in API_timer

vtdaddr         dw 0,0,0                ;far address of the vtd api

bios_cb         dd 0
bios_cb_cs      dw 0
bios_cb_cs_al   dw 0 ;for alignment
bios_cb_real    dd 0

PIT_divisor     dd 0                    ;Last divisor value written to PIT
PIT_period      dd 0                    ;PIT timer interval in uS

entry_count     dd 0

lock_count      dd 0

local_DS        dw 0
local_DS_al     dw 0 ;for alignment

ISR_IRQ         dd 0                    ;Last protected-mode ISR installed by
ISR_selector    dd 0                    ;AIL_set_USE16_ISR()
ISR_offset      dd 0
                
                ALIGN 4                 ;USE16 ISR stack
ISR_stack       db ISR_STACK_SIZE dup (?)

                IF REENTRY_ALLOWED EQ FALSE

                  IF SWITCH_STACK
                  
                    ALIGN 4
                    db 2048 dup (?)     ;2048-byte interrupt stack used by
intstack            LABEL DWORD         ;default -- can be increased if needed

old_ss              dd ?
old_sp              dd ?
                  
                  ENDIF

                ENDIF

                ;
                ;End of locked data
                ;

data_end        LABEL BYTE

                .CODE

                ;
                ;Support services
                ;                

                PUBLIC AILA_startup
                PUBLIC AILA_shutdown

                PUBLIC AIL_API_get_real_vect
                PUBLIC AIL_API_set_real_vect
                PUBLIC AIL_API_set_USE16_ISR
                PUBLIC AIL_API_restore_USE16_ISR

                PUBLIC AIL_API_disable_interrupts
                PUBLIC AIL_API_restore_interrupts

                PUBLIC AIL_switch_stack
                PUBLIC AIL_restore_stack

                PUBLIC AIL_API_call_driver

                PUBLIC AIL_API_background

                ;
                ;Process services
                ;

                PUBLIC AIL_API_register_timer
                PUBLIC AIL_API_set_timer_user
                PUBLIC AIL_API_set_timer_period
                PUBLIC AIL_API_set_timer_frequency
                PUBLIC AIL_API_set_timer_divisor
                PUBLIC AIL_API_interrupt_divisor
                PUBLIC AIL_API_start_timer
                PUBLIC AIL_API_start_all_timers
                PUBLIC AIL_API_stop_timer
                PUBLIC AIL_API_stop_all_timers
                PUBLIC AIL_API_release_timer_handle
                PUBLIC AIL_API_release_all_timers

                PUBLIC AIL_lock
                PUBLIC AIL_unlock

                ;
                ;Start of locked code
                ;

code_start      LABEL BYTE

;#############################################################################
;##                                                                         ##
;## Lock timer interrupt service                                            ##
;##                                                                         ##
;## Timer interrupts will continue to occur, but will not be dispatched     ##
;## to timer clients until the outermost call to AIL_unlock()               ##
;##                                                                         ##
;#############################################################################

AIL_lock        PROC C

                inc lock_count

                ret
AIL_lock        ENDP

;#############################################################################
;##                                                                         ##
;## Unlock timer interrupt service                                          ##
;##                                                                         ##
;#############################################################################

AIL_unlock      PROC C

                dec lock_count

                ret
AIL_unlock      ENDP


;#############################################################################
;##                                                                         ##
;## Get a millisecond accurate count                                        ##
;##                                                                         ##
;#############################################################################

AIL_ms_count PROC C \
                USES ebx

                cmp dword ptr [vtdaddr],0
                jnz __call_vtd
                cmp word ptr [vtdaddr+4],0
                jz __readagain

__call_vtd:     mov eax,0101h
                call fword ptr [vtdaddr]
                jmp __checklast

__readagain:    mov ecx,[timerticks]
                xor al,al
                out 043h,al
                jmp $+2
                in  al,040h
                mov ah,al
                jmp $+2
                in  al,040h
                xchg al,ah
                cmp ecx,[timerticks]
                jne __readagain

                and eax,0ffffh
                jz __zeropit
      
                mov ebx,[PIT_divisor]
                cmp ebx,0
                jne __okdiv

                mov ebx,65536

__okdiv:        sub ebx,eax
                jae __okread

                xor ebx,ebx

__okread:       mov eax,1000
                mul ebx
                mov ebx,1193181
                div ebx

__zeropit:      mov ebx,eax

                mov eax,[PIT_period]
                mul ecx          
                mov ecx,1000
                div ecx

                add eax,ebx

                add eax,[adjstartms]

__checklast:    cmp eax,[lastmscount]
                jae __okcount

                mov eax,[lastmscount]

__okcount:      mov [lastmscount],eax
                
                ret
AIL_ms_count ENDP


;#############################################################################
;##                                                                         ##
;## Get a microsecond accurate count                                        ##
;##                                                                         ##
;#############################################################################

AIL_us_count PROC C \
                USES ebx ecx edx

                cmp dword ptr [vtdaddr],0
                jnz __call_vtd
                cmp word ptr [vtdaddr+4],0
                jz __readagain

__call_vtd:     mov eax,0100h
                call fword ptr [vtdaddr]
                and edx,07ffh ;and out potential overflow
                mov ebx,1000000
                mul ebx
                mov ebx,1193181
                div ebx
                jmp __checklast

__readagain:    mov ecx,[timerticks]
                xor al,al
                out 043h,al
                jmp $+2
                in  al,040h
                mov ah,al
                jmp $+2
                in  al,040h
                xchg al,ah
                cmp ecx,[timerticks]
                jne __readagain

                and eax,0ffffh
                jz __zeropit

                mov ebx,[PIT_divisor]
                cmp ebx,0
                jne __okdiv

                mov ebx,65536

__okdiv:        sub ebx,eax
                jae __okread

                xor ebx,ebx

__okread:       mov eax,1000000
                mul ebx
                mov ebx,1193181
                div ebx

__zeropit:      mov ebx,eax

                mov eax,[PIT_period]
                mul ecx

                add eax,ebx

                add eax,[adjstartus]

__checklast:    
                ret
AIL_us_count ENDP


;#############################################################################
;##                                                                         ##
;## AIL Process Services timer interrupt handler                            ##
;##                                                                         ##
;#############################################################################

setup_ms_count PROC C
               cmp [lastmscount],0
               jz __skipsetup

               invoke AIL_ms_count
               mov [adjstartms],eax

               invoke AIL_us_count
               mov [adjstartus],eax

               mov [timerticks],0
__skipsetup:
               ret
setup_ms_count ENDP


;#############################################################################
;##                                                                         ##
;## AIL Process Services timer interrupt handler                            ##
;##                                                                         ##
;#############################################################################

API_timer       PROC

                push ds
                mov  ds,cs:local_DS  ; setup ds

                inc  [timerticks]

                ;
                ;If reentry not allowed, do not process reentrant interrupts
                ;

                IF REENTRY_ALLOWED EQ FALSE

                  cmp entry_count,0
                  ja __reentered

                ENDIF

                ;
                ;Save all registers and clear direction flag
                ;

                pushad
                push es
                push fs
                push gs
                mov ebp,esp
                cld

		IF SAVE_FPU EQ TRUE

                  sub    esp,108
                  fnsaved [ebp-108]      ; Save 8087 data

		ENDIF

                ;
                ;Load segment registers to access AIL data
                ;

                mov es,[local_DS]

                ;
                ;Switch stacks if requested, and if reentry not allowed
                ;

                inc entry_count

                IF REENTRY_ALLOWED EQ FALSE

                  IF SWITCH_STACK

                    mov old_ss,ss
                    mov old_sp,esp
                    mov ax,ds
                    mov ss,ax           ;MOV into SS disables IRQs temporarily
                    mov esp,OFFSET intstack

                  ENDIF

                ENDIF


                ;
                ; If we're running under windows, get the delta from VTD
                ; otherwise we figure the delta based on timerticks (which
                ; keep ticking even when the timer is locked and re-entry
                ; is denied).
                ;

                cmp dword ptr [vtdaddr],0
                jnz __call_vtd_tmr
                cmp word ptr [vtdaddr+4],0
                jz __use_ticks

__call_vtd_tmr:
                ; call the Windows vtd device

                mov eax,0101h
                call fword ptr [vtdaddr]

                mov ecx,1000  ; store the conversion from msec to usec

                cmp [lastapitimer],0
                jnz __compute_delta

                ; this is the first time through, so save the initial value

                mov edx,[PIT_period]
                mov [lastapitimer],eax
                jmp __ok_delta

__use_ticks:
                ; figure based on the timerticks incrementing variable

                mov eax,[timerticks]

                cmp [lastapitimer],0
                jnz __not_first_ticks

                ; this is the first time through, so save the initial value

                mov edx,[PIT_period]
                mov [lastapitimer],eax
                jmp __ok_delta

__not_first_ticks:

                ; this isn't the first time through, so figure the delta

                mov ecx, [PIT_period] ; store conversion from ticks to usec

__compute_delta:
                ; figure the delta from the last time to this one

                mov edx,eax
                sub eax,[lastapitimer]
                mov [lastapitimer],edx

                ; convert from ticks to microseconds

                xor edx,edx
                mul ecx

                mov edx,eax

                cmp eax,1000000
                jb  __ok_delta
                
                mov edx,1000000

                ;
                ;Advance all active timers
                ;

__ok_delta:     mov edi,0

__for_timer_1:  cmp timer_status[edi],TIMER_RUNNING
                jne __next_timer_1

                mov eax,timer_elapsed[edi]
                add eax,edx

__keep_checking:cmp eax,timer_value[edi]
                jb __advance

                sub eax,timer_value[edi]

                inc timer_trigger[edi]
                jmp __keep_checking

__advance:      mov timer_elapsed[edi],eax

__next_timer_1: add edi,4
                cmp edi,N_TIMERS * 4
                jb __for_timer_1

                ;
                ;Acknowledge interrupt and enable subsequent interrupts
                ;if EARLY_EOI enabled
                ;

                IF EARLY_EOI

                  mov al,20h
                  out 20h,al
                  sti

                ENDIF

                ;
                ;If timer service is locked, do not service AIL timer clients
                ;

                cmp lock_count,0
                jg __BIOS_timer

                ;
                ;Otherwise, call all triggered timers (except BIOS)
                ;

                mov edi,0

__for_timer_2:  cmp timer_trigger[edi],0
                je __next_timer_2

                dec timer_trigger[edi]

                mov eax,timer_user[edi]
                push eax
                call timer_callback[edi]

                jmp __for_timer_2

__next_timer_2: add edi,4
                cmp edi,(N_TIMERS-1) * 4
                jb __for_timer_2

                ;
                ;Chain to previous INT 8 handler if BIOS timer triggered
                ;

__BIOS_timer:   cmp timer_trigger[BIOS_H * 4],0
                je __exit

                dec timer_trigger[BIOS_H * 4]

                IF REENTRY_ALLOWED EQ FALSE

                  IF SWITCH_STACK

                    mov ss,old_ss       ;MOV into SS disables IRQs temporarily
                    mov esp,old_sp

                  ENDIF

                ENDIF

                dec entry_count

                IF SAVE_FPU EQ TRUE

                  frstord [esp]          ; Restore 8087 data
                  add    esp,108

                ENDIF

                IFDEF PHARLAP

                  mov eax,250eh
                  mov ebx,bios_cb_real
                  mov ecx,2
                  pushf
                  int 21h

                ELSE

                  mov ecx,bios_cb       ;ECX = 32-bit offset
                  movzx eax,bios_cb_cs  ;EAX = 16-bit selector

                  mov esp,ebp           ;EBP -> original interrupt stack frame

                  mov ds,[ebp+2ch]      ;JKR: Fix up the return stack by
                  mov [ebp+2ch],eax     ;flipping the registers pushed
                  mov eax,[ebp+28h]     ;with push ds and pushad
                  mov [ebp+28h],ecx     ;(don't try this at home)

                  pop gs
                  pop fs
                  pop es
                  pop edi
                  pop esi
                  pop ebp
                  pop ebx
                  pop ebx
                  pop edx
                  pop ecx
                  retf

                ENDIF

                ;
                ;Restore registers and exit
                ;

__exit:
                IF REENTRY_ALLOWED EQ FALSE

                  IF SWITCH_STACK

                    mov ss,old_ss       ;MOV into SS disables IRQs temporarily
                    mov esp,old_sp

                  ENDIF

                ENDIF

                dec entry_count

                IF EARLY_EOI EQ FALSE

                  mov al,20h
                  out 20h,al

                ENDIF

                IF SAVE_FPU EQ TRUE

                  frstord [esp]          ; Restore 8087 data
                  add    esp,108

                ENDIF

                mov esp,ebp
                pop gs
                pop fs
                pop es
                popad

                pop ds

                iretd

                ;
                ;Acknowledge interrupt and return if illegal re-entry
                ;occurs
                ;

                IF REENTRY_ALLOWED EQ FALSE

__reentered:      push eax
                  mov al,20h
                  out 20h,al
                  pop eax

                  pop ds

                  iretd

                ENDIF

API_timer       ENDP

;#############################################################################
;##                                                                         ##
;## Write to 8253 Programmable Interval Timer chip                          ##
;##                                                                         ##
;#############################################################################

set_PIT_divisor PROC C \                ;Set 8253 Programmable Interval Timer
                ,Divisor:DWORD

                pushfd
                cli

                mov al,34h
                out 43h,al

                mov eax,[Divisor]       ;PIT granularity = 1/1193181 sec.
                mov PIT_divisor,eax

                jmp $+2
                out 40h,al

                mov al,ah

                jmp $+2
                out 40h,al

                POP_F
                ret
set_PIT_divisor ENDP

;#############################################################################
;##                                                                         ##
;## Set 8253 Programmable Interval Timer to desired period in microseconds  ##
;##                                                                         ##
;#############################################################################

set_PIT_period  PROC C \
                USES ebx \
                ,Period:DWORD

                mov eax,0               ;special case: no rounding error
                cmp [Period],54925      ;if period=55 msec. BIOS default value
                jae __set_PIT

                mov eax,[Period]
                mov ebx,8380            ;PIT granularity = .83809532 uS
                mov ecx,10000           ;round down to avoid cumulative error
                mul ecx
                div ebx

__set_PIT:      invoke set_PIT_divisor,eax

                ret
set_PIT_period  ENDP

;#############################################################################
;##                                                                         ##
;## Establish hardware timer interrupt rate based on fastest requested      ##
;## timer                                                                   ##
;##                                                                         ##
;#############################################################################

program_timers  PROC C \                                                
                USES edi es

                invoke AIL_lock

                push ds
                pop es
                cld

                ;
                ;Find fastest active timer
                ;

                mov ecx,-1              ;ECX = smallest period in uS

                mov edi,0           

__for_timer:    cmp timer_status[edi],TIMER_FREE
                je __next_timer

                mov eax,timer_value[edi]

                cmp eax,ecx
                jae __next_timer
                mov ecx,eax

__next_timer:   add edi,4
                cmp edi,(N_TIMERS * 4)  ;(include BIOS reserved timer)
                jb __for_timer

                ;
                ;If current fastest rate equal to old fastest rate, exit...
                ;

                cmp ecx,PIT_period
                je __exit

                ;
                ;...else set new hardware timer rate
                ;

                mov PIT_period,ecx

                invoke set_PIT_period,ecx

                ;
                ;Reset all elapsed time counters to 0 uS
                ;

                mov eax,0          

                mov edi,OFFSET timer_elapsed
                mov ecx,N_TIMERS
                rep stosd

                invoke setup_ms_count

__exit:         invoke AIL_unlock
                ret
program_timers  ENDP

;#############################################################################
;##                                                                         ##
;## Initialize and start up AIL Process Services                            ##
;##                                                                         ##
;#############################################################################

AILA_startup    PROC \                  ;Initialize AIL API
                USES ebx edi es

                pushfd
                cli

                push ds
                pop es
                cld

                mov local_DS,ds
                mov es,local_DS

                call AILA_VMM_lock
                
                mov entry_count,0
                mov lock_count,0

                mov PIT_period,-1

                mov ISR_IRQ,-1

                mov eax,0

                mov edi,OFFSET timer_status
                mov ecx,N_TIMERS
                rep stosd

                mov edi,OFFSET timer_elapsed
                mov ecx,N_TIMERS
                rep stosd
                                                        
                mov edi,OFFSET timer_value
                mov ecx,N_TIMERS
                rep stosd

                mov edi,OFFSET timer_trigger
                mov ecx,N_TIMERS
                rep stosd

                push es                  ;find the VTD device under windows
                mov eax,01684h           ;use it for ms timing instead
                mov ebx,5                ;of reading the timer directly
                xor edi,edi
                mov es,di
                int 02fh
                mov word ptr [vtdaddr],di
                mov word ptr [vtdaddr+2],0
                mov word ptr [vtdaddr+4],es
                pop es

                mov eax,HOOK_INT         ;get current INT 8 vector and save it
                GET_VECT                 ;as reserved BIOS timer function
                mov bios_cb,ebx      
                mov bios_cb_cs,dx
                mov bios_cb_real,ecx

                mov eax,HOOK_INT         ;replace default handler with API task
                mov edx,OFFSET API_timer ;manager
                mov bx,cs
                SET_VECT

                mov timer_status[BIOS_H * 4],TIMER_RUNNING

                invoke AIL_set_timer_period,BIOS_H * 4,54925

                POP_F
                ret
AILA_startup    ENDP

;#############################################################################
;##                                                                         ##
;## Shut down all AIL Process Services                                      ##
;##                                                                         ##
;#############################################################################

AILA_shutdown   PROC \
                USES ebx 

                pushfd
                cli

                invoke set_PIT_divisor,0

                IFDEF INT21             ;reset real-mode vector only under
                mov eax,HOOK_INT        ;Phar Lap/FlashTek
                mov ebx,bios_cb_real
                SET_REAL_VECT
                ENDIF

                mov eax,HOOK_INT
                mov edx,bios_cb
                movzx ebx,bios_cb_cs
                SET_PROT_VECT

                POP_F
                ret
AILA_shutdown   ENDP

;#############################################################################
;##                                                                         ##
;## Get a real-mode interrupt vector                                        ##
;##                                                                         ##
;#############################################################################

AIL_API_get_real_vect PROC \
                USES ebx \
                ,VecNum

                mov eax,[VecNum]

                GET_VECT

                mov eax,ecx
                ret

AIL_API_get_real_vect ENDP

;#############################################################################
;##                                                                         ##
;## Set a real-mode interrupt vector                                        ##
;##                                                                         ##
;#############################################################################

AIL_API_set_real_vect PROC \
                USES ebx \
                ,VecNum,RealPtr

                mov eax,[VecNum]
                mov ebx,[RealPtr]

                SET_REAL_VECT
                ret

AIL_API_set_real_vect ENDP

;#############################################################################
;##                                                                         ##
;## Declare 16-bit protected-mode ISR                                       ##
;##                                                                         ##
;#############################################################################

thunk_proc      PROC

                push ds
                push es
                push eax
                push ebx
                push ecx

                IF SHOW_MODE

norm_DS           LABEL BYTE
                  mov ax,9999h
                  mov ds,ax
                  inc BYTE PTR ds:[0b8002h]

                ENDIF

ISR_data        LABEL BYTE
                mov ax,9999h
                mov ds,ax
                mov es,ax

                mov bx,ss
                mov ecx,esp

ISR_SS          LABEL BYTE
                mov ax,9999h
                mov ss,ax

ISR_SP          LABEL BYTE
                mov esp,99999999h

                pushf
                pop ax
                mov [esp-2],ax
                mov [esp-4],cs
thunk_off       LABEL BYTE
                mov WORD PTR [esp-6],9999h
                sub esp,6

ISR_code        LABEL BYTE
                mov WORD PTR [esp-2],9999h
ISR_off         LABEL BYTE
                mov WORD PTR [esp-4],9999h
                sub esp,4
                db 66h
                retf

thunk_exit      LABEL BYTE

                mov ss,bx
                mov esp,ecx

                pop ecx
                pop ebx
                pop eax
                pop es
                pop ds

                iretd

thunk_proc      ENDP

;*****************************************************************************
AIL_API_set_USE16_ISR PROC \
                USES ebx esi edi es \  
                ,IRQ,RealBase,ISROffset

                LOCAL linear
                LOCAL ISR_code_sel
                LOCAL ISR_data_sel
                LOCAL ISR_stack_sel
                LOCAL ISR_stack_off
                LOCAL thunk_code_sel
                LOCAL thunk_proc_off

                ;
                ;Fail call if ISR vector already set (only one thunk
                ;available, so this function can be used only once)
                ;

                cmp ISR_IRQ,-1
                jne __exit

                IFDEF DPMI

                  ;
                  ;Calculate linear address of low-memory ISR
                  ;
                  
                  mov edx,[RealBase]
                  mov eax,edx
                  and edx,0ffffh
                  shr eax,16
                  shl eax,4
                  add eax,edx             ;EAX = linear base address
                  mov linear,eax

                  ;
                  ;Allocate 16-bit code selector
                  ;
                  
                  mov eax,0000h           ;allocate LDT descriptor (selector)
                  mov ecx,1               ;from DPMI
                  int 31h
                  and eax,0ffffh
                  mov ISR_code_sel,eax
                  
                  mov eax,7               ;set linear address via DPMI
                  mov ebx,ISR_code_sel
                  mov cx,WORD PTR linear+2
                  mov dx,WORD PTR linear
                  int 31h
                  
                  mov eax,8               ;set limit via DPMI
                  mov ebx,ISR_code_sel
                  mov ecx,0
                  mov edx,0ffffh
                  int 31h
                  
                  mov eax,9               ;set access rights via DPMI
                  mov ebx,ISR_code_sel
                  mov edx,ds
                  lar ecx,edx
                  shr ecx,8               ;lower 8 bits inherited from DS
                  or  cl,00001000b        ;set "executable" flag
                  mov ch,00000000b        ;upper 8 access bits = USE16
                  int 31h
                  
                  ;
                  ;Allocate 16-bit data selector
                  ;
                  
                  mov eax,0000h           ;allocate LDT descriptor (selector)
                  mov ecx,1               ;from DPMI
                  int 31h
                  and eax,0ffffh
                  mov ISR_data_sel,eax
                  
                  mov eax,7               ;set linear address via DPMI
                  mov ebx,ISR_data_sel
                  mov cx,WORD PTR linear+2
                  mov dx,WORD PTR linear
                  int 31h
                  
                  mov eax,8               ;set limit via DPMI
                  mov ebx,ISR_data_sel
                  mov ecx,0
                  mov edx,0ffffh
                  int 31h
                  
                  mov eax,9               ;set access rights via DPMI
                  mov ebx,ISR_data_sel
                  mov edx,ds
                  lar ecx,edx
                  shr ecx,8               ;lower 8 bits inherited from DS
                  mov ch,00000000b        ;upper 8 access bits = USE16
                  int 31h

                  ;
                  ;Allocate 16-bit stack selector
                  ;
                  ;Selector alias needed to ensure that (E)SP < 64K when 
                  ;16-bit ISR called
                  ;
                  
                  mov eax,0000h           ;allocate LDT descriptor (selector)
                  mov ecx,1               ;from DPMI
                  int 31h
                  and eax,0ffffh
                  mov ISR_stack_sel,eax

                  mov ecx,OFFSET ISR_stack
                  and ecx,0fffffff0h
                  
                  mov eax,OFFSET ISR_stack
                  sub eax,ecx
                  mov ISR_stack_off,eax
                  
                  mov eax,7               ;set (para-aligned) linear address
                  mov ebx,ISR_stack_sel
                  mov edx,ecx
                  and edx,0ffffh
                  shr ecx,16
                  int 31h
                  
                  mov eax,8               ;set limit via DPMI
                  mov ebx,ISR_stack_sel
                  mov ecx,0
                  mov edx,0ffffh
                  int 31h
                  
                  mov eax,9               ;set access rights via DPMI
                  mov ebx,ISR_stack_sel
                  mov edx,ds
                  lar ecx,edx
                  shr ecx,8               ;lower 8 bits inherited from DS
                  mov ch,00000000b        ;upper 8 access bits = USE16
                  int 31h
                  
                  ;
                  ;Allocate 32-bit thunk code selector alias 
                  ;
                  ;Selector alias needed to ensure that (E)IP < 64K when 
                  ;16-bit ISR called
                  ;
                  
                  mov eax,0000h           ;allocate LDT descriptor (selector)
                  mov ecx,1               ;from DPMI
                  int 31h
                  and eax,0ffffh
                  mov thunk_code_sel,eax
                  
                  mov ecx,OFFSET thunk_proc
                  and ecx,0fffffff0h

                  mov eax,OFFSET thunk_proc
                  sub eax,ecx
                  mov thunk_proc_off,eax
                  
                  mov eax,7               ;set (para-aligned) linear address
                  mov ebx,thunk_code_sel
                  mov edx,ecx
                  and edx,0ffffh
                  shr ecx,16
                  int 31h

                  mov eax,8               ;set limit via DPMI
                  mov ebx,thunk_code_sel
                  mov ecx,0ffffh
                  mov edx,0ffffh
                  int 31h
                  
                  mov eax,9               ;set access rights via DPMI
                  mov ebx,thunk_code_sel
                  mov edx,ds
                  lar ecx,edx
                  shr ecx,8               ;lower 8 bits inherited from DS
                  or  cl,00001000b        ;set "executable" flag
                  mov ch,11000000b        ;upper 8 bits = USE32 page-gr.
                  int 31h
                  
                  ;
                  ;Perform thunk fixups to call 16-bit ISR
                  ;
                  
                  mov eax,ISR_stack_off
                  add eax,ISR_STACK_SIZE
                  mov DWORD PTR ISR_SP+1,eax
                  
                  mov eax,ISR_stack_sel
                  mov WORD PTR ISR_SS+2,ax
                  
                  mov eax,ISR_data_sel
                  mov WORD PTR ISR_data+2,ax
                  
                  mov eax,ISR_code_sel
                  mov WORD PTR ISR_code+5,ax
                  
                  mov eax,[ISROffset]
                  mov WORD PTR ISR_off+5,ax
                  
                  mov eax,(OFFSET thunk_exit - OFFSET thunk_proc)
                  add eax,thunk_proc_off
                  mov WORD PTR thunk_off+5,ax
                  
                  IF SHOW_MODE
                  
                    mov eax,ds
                    mov WORD PTR norm_DS+2,ax
                  
                  ENDIF
                  
                  ;
                  ;Preserve protected-mode interrupt vector, and set
                  ;new vector to point to thunk
                  ;
                  
                  mov ebx,[IRQ]
                  
                  mov ISR_IRQ,ebx
                  
                  cmp ebx,8
                  jb __calc_vect
                  add ebx,60h
__calc_vect:      add ebx,8
                  
                  push ebx
                  
                  mov eax,204h
                  int 31h
                  and ecx,0ffffh
                  mov ISR_selector,ecx
                  mov ISR_offset,edx

                  pop ebx
                  
                  mov eax,205h
                  mov ecx,thunk_code_sel
                  mov edx,thunk_proc_off
                  int 31h

                ENDIF

__exit:         ret

AIL_API_set_USE16_ISR\
                ENDP

;#############################################################################
;##                                                                         ##
;## Restore previous protected-mode ISR vector                              ##
;##                                                                         ##
;#############################################################################

AIL_API_restore_USE16_ISR PROC \
                USES ebx esi edi es \  
                ,IRQ

                IFDEF DPMI

                  mov ebx,[IRQ]
                  
                  cmp ebx,-1
                  je __exit
                  
                  cmp ebx,ISR_IRQ
                  jne __exit

                  cmp ebx,8
                  jb __calc_vect
                  add ebx,60h
__calc_vect:      add ebx,8

                  mov eax,205h
                  mov ecx,ISR_selector
                  mov edx,ISR_offset
                  int 31h

                ENDIF

                mov ISR_IRQ,-1

__exit:         ret

AIL_API_restore_USE16_ISR \
                ENDP

;#############################################################################
;##                                                                         ##
;## Disable hardware interrupts                                             ##
;##                                                                         ##
;#############################################################################

AIL_API_disable_interrupts PROC

                pushfd
                pop eax
                
                cli

                ret
AIL_API_disable_interrupts ENDP

;#############################################################################
;##                                                                         ##
;## Restore previous interrupt state                                        ##
;##                                                                         ##
;#############################################################################

AIL_API_restore_interrupts PROC \
                FD_register:DWORD

                push FD_register
                POP_F

                ret
AIL_API_restore_interrupts ENDP

;#############################################################################
;##                                                                         ##
;## Switch to an alternate stack                                            ##
;##                                                                         ##
;#############################################################################

EBP_addr        equ [esp+20]
ESP_addr        equ [esp+16]
SS_addr         equ [esp+12]
StkSize         equ [esp+8]
Stk             equ [esp+4]

AIL_switch_stack PROC

                mov eax,ESP_addr        ;save stack pointer and EBP at time
                mov [eax],esp           ;of call

                mov eax,EBP_addr
                mov [eax],ebp

                mov eax,SS_addr
                mov [eax],ss

                mov eax,Stk
                add eax,StkSize         ;set EAX -> end of new stack
                and eax,NOT 4           ;force DWORD alignment

                mov edx,[esp]           ;EDX = return address
                add esp,(6*4)           ;remove call and parms from stack
                sub ebp,esp             ;EBP = size of local variable space

                mov ecx,ds              ;set up new stack
                mov ss,ecx              ;('mov' into SS disables IRQs until
                mov esp,eax             ;next instruction finished)

                mov eax,ebp
                mov ebp,esp             ;construct new stack frame
                sub esp,eax             ;allocate local variable space

                sub esp,(5*4)           ;simulate parameters on new stack and
                jmp edx                 ;return

AIL_switch_stack ENDP

;#############################################################################
;##                                                                         ##
;## Return to the original C stack                                          ##
;##                                                                         ##
;#############################################################################

AIL_restore_stack PROC \
                SS_reg:DWORD,ESP_reg:DWORD,EBP_reg:DWORD

                mov ecx,[esp+4]         ;ECX = return address

                mov eax,[SS_reg]
                mov edx,[ESP_reg]
                mov ebp,[EBP_reg]

                add edx,(6*4)           ;remove AIL_switch_stack() call

                mov ss,eax
                mov esp,edx

                sub esp,(3*4)           ;simulate parameters on new stack and
                jmp ecx                 ;return

AIL_restore_stack ENDP

;#############################################################################
;##                                                                         ##
;## Issue call to real-mode Vendor Device Interface driver layer            ##
;##                                                                         ##
;#############################################################################

AIL_API_call_driver PROC C \
                USES ebx esi edi ds es \
                ,Drvr:PTR,FnNum:DWORD,VDI_IN:PTR,VDI_OUT:PTR
                LOCAL regEBX,regECX
                LOCAL RM:RMI

                invoke AIL_lock

                cld

                mov ecx,SIZE RMI        ;init RMI structure (on stack to
                                        ;permit re-entrant calls)
                push ds
                pop es
                lea edi,RM

                mov eax,0
                mov bl,cl
                shr ecx,2
                and bl,3
                rep stosd
                mov cl,bl
                rep stosb

                IFDEF INT21

                  mov eax,[FnNum]
                  mov RM.register_eax,eax

                  mov ebx,[VDI_IN]
                  cmp ebx,0
                  je __get_handle

                  mov cx,[ebx].VDI_CALL.VDI_CX
                  mov dx,[ebx].VDI_CALL.VDI_DX
                  mov si,[ebx].VDI_CALL.VDI_SI
                  mov di,[ebx].VDI_CALL.VDI_DI

                  mov RM.register_edx,edx

__get_handle:     mov eax,ds
                  mov es,eax
                  mov ebx,[Drvr]
                  mov ebx,es:[ebx].AIL_DRIVER.dbuf
                  movsx ebx,es:[ebx].VDI_HDR.driver_num
                  
                  mov RM.interrupt_num,66h   ;INT 66H
                  mov eax,2511h
                  lea edx,RM
                  int 21h
                  
                  mov dx,bx
                  
                  mov ebx,[VDI_OUT]
                  cmp ebx,0
                  je __exit
                  
                  mov [ebx].VDI_CALL.VDI_AX,ax
                  mov [ebx].VDI_CALL.VDI_BX,dx
                  mov [ebx].VDI_CALL.VDI_CX,cx
                  mov [ebx].VDI_CALL.VDI_SI,si
                  mov [ebx].VDI_CALL.VDI_DI,di
                  
                  mov edi,RM.register_edx
                  mov [ebx].VDI_CALL.VDI_DX,di
                  
__exit:           cwde
                  
                ELSEIFDEF DPMI

                  mov RM.int_DS,0
                  mov RM.int_ES,0
                  pushf
                  pop ax
                  mov RM.int_flags,ax
                  
                  mov eax,[FnNum]
                  mov RM.int_AX,ax

                  mov ebx,[VDI_IN]
                  cmp ebx,0
                  je __get_handle
                  
                  mov cx,[ebx].VDI_CALL.VDI_CX
                  mov dx,[ebx].VDI_CALL.VDI_DX
                  mov si,[ebx].VDI_CALL.VDI_SI
                  mov di,[ebx].VDI_CALL.VDI_DI
                  
                  mov RM.int_CX,cx
                  mov RM.int_DX,dx
                  mov RM.int_SI,si
                  mov RM.int_DI,di
                  
__get_handle:     mov ebx,[Drvr]
                  mov ebx,[ebx].AIL_DRIVER.dbuf
                  movsx ebx,[ebx].VDI_HDR.driver_num
                  
                  mov RM.int_BX,bx
                  
                  mov eax,300h 
                  mov ebx,66h             ;INT 66h
                  mov ecx,0
                  lea edi,RM
                  int 31h                 
                  
                  mov ebx,[VDI_OUT]
                  cmp ebx,0
                  je __exit
                  
                  mov ax,RM.int_AX
                  mov cx,RM.int_CX
                  mov dx,RM.int_DX
                  mov si,RM.int_SI
                  mov di,RM.int_DI
                  
                  mov [ebx].VDI_CALL.VDI_AX,ax
                  mov [ebx].VDI_CALL.VDI_CX,cx
                  mov [ebx].VDI_CALL.VDI_DX,dx
                  mov [ebx].VDI_CALL.VDI_SI,si
                  mov [ebx].VDI_CALL.VDI_DI,di
                  
                  mov ax,RM.int_BX
                  mov [ebx].VDI_CALL.VDI_BX,ax
                  
__exit:           movsx eax,RM.int_AX

                ENDIF

                push eax
                invoke AIL_unlock
                pop eax

                ret

AIL_API_call_driver ENDP

;#############################################################################
;##                                                                         ##
;## Return nonzero value if currently executing from within timer tick      ##
;## handler, else 0                                                         ##
;##                                                                         ##
;#############################################################################

AIL_API_background  PROC 
                mov eax,entry_count
                ret

AIL_API_background  ENDP

;#############################################################################
;##                                                                         ##
;## Register an application function as a timer callback routine, and       ##
;## obtain a timer handle which may be used to control the timer            ##
;##                                                                         ##
;#############################################################################

AIL_API_register_timer PROC C \
                ,Callback:DWORD

                invoke AIL_lock

                mov eax,0               ;look for a free timer handle....

__find_free:    cmp timer_status[eax],TIMER_FREE
                je __found              ;found one

                add eax,4
                cmp eax,(N_TIMERS-1) * 4
                jb __find_free

                mov eax,-1
                jmp __return            ;no free timers, return -1

__found:        mov timer_status[eax],TIMER_STOPPED

                mov ecx,[Callback]      ;register the timer proc
                mov timer_callback[eax],ecx

__return:       push eax
                invoke AIL_unlock
                pop eax

                ret
AIL_API_register_timer ENDP

;#############################################################################
;##                                                                         ##
;## Associate any 32-bit user value with a timer handle                     ##
;##                                                                         ##
;## The user value is passed as an optional parameter to the timer's        ##
;## callback function                                                       ##
;##                                                                         ##
;#############################################################################

AIL_API_set_timer_user PROC C \
                ,Timer:TIMER_TYPE,User:DWORD

                invoke AIL_lock

                mov ecx,[Timer]
                cmp ecx,-1
                je __return

                mov eax,[User]
                xchg timer_user[ecx],eax

__return:       push eax
                invoke AIL_unlock
                pop eax

                ret
AIL_API_set_timer_user ENDP

;#############################################################################
;##                                                                         ##
;## Release a registered timer handle                                       ##
;##                                                                         ##
;#############################################################################

AIL_API_release_timer_handle PROC C \
                ,Timer:TIMER_TYPE

                invoke AIL_lock

                mov eax,[Timer]
                cmp eax,-1
                je __return

                mov timer_status[eax],TIMER_FREE

__return:       invoke AIL_unlock
                ret
AIL_API_release_timer_handle ENDP

;#############################################################################
;##                                                                         ##
;## Release all registered timer handles                                    ##
;##                                                                         ##
;#############################################################################

AIL_API_release_all_timers PROC C \
                uses esi

                invoke AIL_lock

                mov esi,(N_TIMERS-2) * 4

__release_it:   invoke AIL_release_timer_handle,esi

                sub esi,4
                jge __release_it

                invoke AIL_unlock
                ret
AIL_API_release_all_timers ENDP

;#############################################################################
;##                                                                         ##
;## Start a stopped timer                                                   ##
;##                                                                         ##
;#############################################################################

AIL_API_start_timer PROC C \
                ,Timer:TIMER_TYPE

                invoke AIL_lock

                mov ecx,[Timer]
                cmp ecx,-1
                je __return

                cmp timer_status[ecx],TIMER_STOPPED
                jne __return

                mov timer_status[ecx],TIMER_RUNNING
__return:       
                invoke AIL_unlock
                ret
AIL_API_start_timer ENDP

;#############################################################################
;##                                                                         ##
;## Start all stopped timers                                                ##
;##                                                                         ##
;#############################################################################

AIL_API_start_all_timers PROC C \
                uses esi 

                invoke AIL_lock

                mov esi,(N_TIMERS-2) * 4

__release_it:   invoke AIL_start_timer,esi

                sub esi,4
                jge __release_it

                invoke AIL_unlock
                ret
AIL_API_start_all_timers ENDP

;#############################################################################
;##                                                                         ##
;## Stop a running timer                                                    ##
;##                                                                         ##
;#############################################################################

AIL_API_stop_timer  PROC C \
                ,Timer:TIMER_TYPE

                invoke AIL_lock

                mov ecx,[Timer]
                cmp ecx,-1
                je __return

                cmp timer_status[ecx],TIMER_RUNNING
                jne __return

                mov timer_status[ecx],TIMER_STOPPED
__return:       
                invoke AIL_unlock
                ret
AIL_API_stop_timer  ENDP

;#############################################################################
;##                                                                         ##
;## Stop all running timers                                                 ##
;##                                                                         ##
;#############################################################################

AIL_API_stop_all_timers PROC C \
                uses esi

                invoke AIL_lock

                mov esi,(N_TIMERS-2) * 4

__release_it:   invoke AIL_stop_timer,esi

                sub esi,4
                jge __release_it

                invoke AIL_unlock
                ret
AIL_API_stop_all_timers ENDP

;#############################################################################
;##                                                                         ##
;## Set timer rate as period in microseconds                                ##
;##                                                                         ##
;#############################################################################

AIL_API_set_timer_period PROC C \
                USES ebx esi edi \ 
                ,Timer:TIMER_TYPE,uS:DWORD

                invoke AIL_lock

                mov ecx,[Timer]
                mov eax,[uS]

                mov timer_value[ecx],eax
                mov timer_elapsed[ecx],0

                call program_timers     ;reset base interrupt rate if needed

                invoke AIL_unlock
                ret
AIL_API_set_timer_period ENDP

;#############################################################################
;##                                                                         ##
;## Set timer rate as frequency in hertz                                    ##
;##                                                                         ##
;#############################################################################

AIL_API_set_timer_frequency PROC C \
                ,Timer:TIMER_TYPE,Hz:DWORD

                invoke AIL_lock

                mov edx,0
                mov eax,0f4240h
                mov ecx,[Hz]
                div ecx

                invoke AIL_set_timer_period,[Timer],eax

                invoke AIL_unlock
                ret
AIL_API_set_timer_frequency ENDP

;#############################################################################
;##                                                                         ##
;## Set timer rate as equivalent PIT value                                  ##
;##                                                                         ##
;#############################################################################

AIL_API_set_timer_divisor PROC C \
                ,Timer:TIMER_TYPE,PIT:DWORD

                invoke AIL_lock

                cmp [PIT],0             ;special case: 0 wraps to 65536
                jne __nonzero
                mov eax,54925      
                jmp __set_AXDX

__nonzero:      mov eax,10000           ;convert to microseconds
                mov edx,0
                mov ecx,11932
                mul [PIT]
                div ecx                 ;(accurate to ñ.01%)

__set_AXDX:     invoke AIL_set_timer_period,[Timer],eax

                invoke AIL_unlock
                ret
AIL_API_set_timer_divisor ENDP

;#############################################################################
;##                                                                         ##
;## Get value last used by API to program the PIT chip                      ##
;##                                                                         ##
;#############################################################################

AIL_API_interrupt_divisor PROC C

                mov eax,PIT_divisor

                ret
AIL_API_interrupt_divisor ENDP


;#############################################################################
;##                                                                         ##
;## Simple file I/O routines                                                ##
;##                                                                         ##
;#############################################################################

firstparam equ ebp+08h
secondparam equ ebp+0ch
thirdparam equ ebp+10h
forthparam equ ebp+14h
fifthparam equ ebp+18h

Public AIL_ferase
AIL_ferase proc ;u32 ofs
  push ebp
  mov ebp,esp
  push ebx
  push edx
  mov ah,041h
  mov edx,[firstparam]
  int 021h
  pop edx
  pop ebx
  pop ebp
  ret
AIL_ferase endp

Public AIL_lowfopen
AIL_lowfopen proc ; u32 ofs,u8 acc
  push ebp
  mov ebp,esp
  push ebx
  push edx
  mov ah,03dh
  mov al,[secondparam]
  mov edx,[firstparam]
  int 021h
  jnc @@cont
  mov ax,0ffffh
 @@cont:
  movsx eax,ax
  pop edx
  pop ebx
  pop ebp
  ret
AIL_lowfopen endp

Public AIL_fcreate
AIL_fcreate proc ;u32 ofs
  push ebp
  mov ebp,esp
  push ebx
  push edx
  mov ah,03ch
  mov edx,[firstparam]
  xor ecx,ecx
  int 021h
  jnc @@cont
  mov ax,0ffffh
 @@cont:
  movsx eax,ax
  pop edx
  pop ebx
  pop ebp
  ret
AIL_fcreate endp    

Public AIL_fappend
AIL_fappend proc ; u32 ofs
  push ebp
  mov ebp,esp
  push ebx
  push edx
  push ecx
  mov ax,03d01h
  mov edx,[firstparam]
  int 021h
  jnc @@cont

  cmp ax,2
  je  @@trycreate

 @@err:
  mov eax,-1
  jmp @@done

 @@trycreate:
  mov ah,03ch
  xor ecx,ecx
  int 021h
  jc @@err

  movsx eax,ax
  jmp @@done

 @@cont:
  movsx ebx,ax
  xor edx,edx
  xor ecx,ecx
  mov ax,04202h
  int 021h
  mov eax,ebx

 @@done:
  pop ecx
  pop edx
  pop ebx
  pop ebp
  ret
AIL_fappend endp


Public AIL_fread
AIL_fread proc ;u32 hand, u32 ofs, u16 seg, u32 bytes
  push ebp
  mov ebp,esp
  push ebx
  push ecx
  push edx
  
  mov edx,[secondparam]
 @@lp:
  mov ah,03fh
  mov ecx,[thirdparam]
  cmp ecx,0ffffh
  jbe @@small
  mov ecx,0f000h
 @@small:
  mov ebx,[firstparam]
  push ecx
  push edx
  int 021h
  pop edx
  pop ecx
  jc @@dne
  and eax,0ffffh
  jz @@dne
  add edx,eax
  cmp eax,ecx
  jb  @@dne
  sub [thirdparam],eax
  jnz @@lp
 @@dne:
  sub edx,[secondparam]
  mov eax,edx

  pop edx
  pop ecx
  pop ebx
  pop ebp
  ret
AIL_fread endp

Public AIL_fwrite
AIL_fwrite proc ;u32 hand, u32 ofs, u32 bytes
  push ebp
  mov ebp,esp
  push ebx
  push ecx
  push edx

  mov edx,[secondparam]
 @@lp:
  mov ah,040h
  mov ecx,[thirdparam]
  cmp ecx,0ffffh
  jbe @@small
  mov ecx,0f000h
 @@small:
  mov ebx,[firstparam]
  push ecx
  push edx
  int 021h
  pop edx
  pop ecx
  jc @@dne
  and eax,0ffffh
  jz @@dne
  add edx,eax
  cmp eax,ecx
  jb  @@dne
  sub [thirdparam],eax
  jnz @@lp
 @@dne:
  sub edx,[secondparam]
  mov eax,edx

  pop edx
  pop ecx
  pop ebx
  pop ebp
  ret
AIL_fwrite endp  

Public AIL_lowfseekbegin
AIL_lowfseekbegin proc ;u32 hand, u32 pos
  mov ax,4200h
 seek::
  push ebp
  mov  ebp,esp
  push ebx
  push ecx
  push edx
  mov ebx,[firstparam]
  movzx edx,word ptr [secondparam]
  movzx ecx,word ptr [secondparam+2]
  int 021h
  shl edx,16
  mov dx,ax
  mov eax,edx
  pop ecx
  pop edx
  pop ebx
  pop ebp
  ret
AIL_lowfseekbegin endp

Public AIL_lowfseekcur
AIL_lowfseekcur proc  ;u32 hand, u32 pos
  mov ax,04201h
  jmp seek
AIL_lowfseekcur endp

Public AIL_lowfseekend
AIL_lowfseekend proc  ;u32 hand, u32 pos
  mov ax,04202h
  jmp seek
AIL_lowfseekend endp

Public AIL_lowfclose
AIL_lowfclose proc ;u32 hand
  push ebp
  mov ebp,esp
  push ebx
  mov ah,03eh
  mov ebx,[firstparam]
  int 021h
  pop ebx
  pop ebp
  ret
AIL_lowfclose endp

Public mult64addandshift
mult64addandshift proc ; U32 mt1, U32 mt2, U32 addv, U32 shift )
  push ebp
  mov ebp,esp
  push edx
  push ecx
  mov eax,[firstparam]
  mov ecx,[secondparam]
  mul ecx
  add eax,[thirdparam]
  adc edx,0
  mov ecx,[forthparam]
  shrd eax,edx,cl
  pop ecx
  pop edx
  pop ebp
  ret
mult64addandshift endp

Public mult64anddiv
mult64anddiv proc; (U32 a,U32 b, U32 c)
  push ebp
  mov ebp,esp
  push edx
  push ecx
  mov eax,[firstparam]
  mov ecx,[secondparam]
  mul ecx
  mov ecx,[thirdparam]
  div ecx
  pop ecx
  pop edx
  pop ebp
  ret
mult64anddiv endp

Public shift64addsubanddiv
shift64addsubanddiv proc ;( U32 val, U32 shift,U32 addv, U32 subv, U32 divv)
  push ebp
  mov ebp,esp
  push edx
  push ecx
  xor edx, edx
  mov eax, [firstparam]
  mov ecx, [secondparam]
  shld edx, eax, cl
  shl eax, cl
  add eax, [thirdparam]
  adc edx, 0
  sub eax, [forthparam]
  sbb edx, 0
  mov ecx, [fifthparam]
  div ecx
  pop ecx
  pop edx
  pop ebp
  ret
shift64addsubanddiv endp



Public AIL_memmove
AIL_memmove proc; void* dest, void* src, u32 len
  push ebp
  mov ebp,esp
  push edi
  push esi
  push ecx

  mov edi,[firstparam]
  mov esi,[secondparam]
  mov ecx,[thirdparam]

  cmp edi,esi
  je  @@dne
  jb  @@normal

  lea eax,[esi+ecx]
  cmp eax,edi
  ja @@back

@@normal:

  cld
  mov al,cl
  shr ecx,2
  and al,3
  rep movsd
  mov cl,al
  rep movsb
  jmp @@dne

@@back:
  std
  mov al,cl
  lea esi,[esi+ecx-4]
  lea edi,[edi+ecx-4]
  shr ecx,2
  rep movsd
  and al,3
  jz @@dback
  add esi,3
  add edi,3
  mov cl,al
  rep movsb
 @@dback:
  cld

@@dne:
  pop ecx
  pop esi
  pop edi
  pop ebp
  ret
AIL_memmove endp


db 13,10,13,10
db 'Miles Sound System',13,10
db 'Copyright (C) 1991-98 RAD Game Tools, Inc.',13,10,13,10


                ;
                ;End of locked code
                ;

code_end        LABEL BYTE


;#############################################################################
;##                                                                         ##
;## Lock all code and data in AILA module                                   ##
;##                                                                         ##
;#############################################################################

AILA_VMM_lock   PROC

                invoke AIL_vmm_lock_range,OFFSET data_start,OFFSET data_end
                invoke AIL_vmm_lock_range,OFFSET code_start,OFFSET code_end

                ret

AILA_VMM_lock   ENDP

                END

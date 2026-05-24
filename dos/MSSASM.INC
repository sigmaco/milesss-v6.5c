
                ;
                ;Macros, internal equates
                ;

                ;
                ;Internal equates
                ;

DMA_SIGN                 equ 1
DMA_ORDER                equ 2
DMA_POLARITY             equ 4
DMA_SPLIT                equ 8
DMA_STEREO               equ 16
DMA_16                   equ 32
DMA_CLIP                 equ 64

M_DEST_STEREO            equ 1
M_RESAMPLE               equ 2
M_SCALE                  equ 4
M_SRC_STEREO             equ 8
M_SRC_UNSIGN             equ 16 ; NEVER USED BY ADPCM
M_SRC_8                  equ 32 ; NEVER USED BY ADPCM
M_SRC_ORDER              equ 64 ; NEVER USED BY ADPCM
M_SRC_ADPCM              equ 128

                ;
                ;Format modes
                ;

DIG_F_16BITS_MASK        equ 1
DIG_F_STEREO_MASK        equ 2
DIG_F_ADPCM_MASK         equ 4

DIG_F_MONO_8             equ 0
DIG_F_MONO_16            equ DIG_F_16BITS_MASK
DIG_F_STEREO_8           equ DIG_F_STEREO_MASK
DIG_F_STEREO_16          equ DIG_F_STEREO_MASK+DIG_F_16BITS_MASK

DIG_PCM_SIGN             equ 0001h
DIG_PCM_ORDER            equ 0002h
DIG_PCM_POLARITY         equ 0004h
DIG_PCM_SPLIT            equ 0008h
DIG_BUFFER_SERVICE       equ 0010h
DIG_DUAL_DMA             equ 0020h
DIG_RECORDING_SUPPORTED  equ 8000h

                ;
                ;Preference equates
                ;

DIG_RESAMPLING_TOLERANCE equ 0 * 4


FALSE           equ 0
TRUE            equ -1

                ;
                ;AIL_DRIVER structure
                ;

AIL_DRIVER      STRUC
dseg            dd ?
dsel            dd ?
dbuf            dd ?
drvrsize        dd ?
vhdr            dd ?
dtype           dd ?
initialized     dd ?
PM_ISR          dd ?
server          dd ?
destructor      dd ?
descriptor      dd ?
AIL_DRIVER      ENDS

                ;
                ;VDI_HDR structure
                ;

VDI_HDR         STRUC
drvr_ID         db 8 dup (?)
driver_version  dd ?
common_configs  dd ?
num_configs     dw ?
env_name        dd ?
IO_ADDR         dw ?                            ;IO_PARMS structure for
IRQ             dw ?                            ;driver -- explicit variables
VHDMA_8         dw ?                            ;used for MASM/TASM
VHDMA_16        dw ?                            ;compatibility
IO_reserved     dd 4 dup (?)
service_rate    dw ?
busy            dw ?
driver_num      dw ?
this_ISR        dw ?
previous_ISR    dd ?
scratch         db 128 dup (?)
VDI_HDR         ENDS

                ;
                ;VDI_CALL structure
                ;

VDI_CALL        STRUC
VDI_AX          dw ?
VDI_BX          dw ?
VDI_CX          dw ?
VDI_DX          dw ?
VDI_SI          dw ?
VDI_DI          dw ?
VDI_CALL        ENDS

                ;
                ;IO_PARMS structure
                ;

IO_PARMS        STRUC
IP_IO_ADDR      dw ?
IP_IRQ          dw ?
IP_DMA_8        dw ?
IP_DMA_16       dw ?
IP_reserved     dw ?
                dw ?
                dw ?
                dw ?
IO_PARMS        ENDS

                ;
                ;MODE structure
                ;

MODE            STRUC
min_phys_rate   dw ?
nom_phys_rate   dw ?
max_phys_rate   dw ?

min_half_size   dw ?
max_half_size   dw ?

mode_flags      dd ?
MODE            ENDS

                ;
                ;DIG_DDT structure
                ;

DIG_DDT         STRUC

format_support  db 16 dup (?)
format_data     MODE 16 dup (<>)

DIG_DDT         ENDS

                ;
                ;DIG_DST structure
                ;

DIG_DST         STRUC

DMA_buffer_A    dd ?
DMA_buffer_B    dd ?

active_buffer   dw ?

DIG_DST         ENDS

                ;
                ;MDI_DDT structure
                ;

MDI_DDT         STRUC
MDI_DDT         ENDS

                ;
                ;MDI_DST structure
                ;

MDI_DST         STRUC
MDI_DST         ENDS

                ;
                ;Real-mode interrupt call structures
                ;

                IFDEF DPMI

RMI             STRUC                   ;DPMI real-mode interrupt structure
int_DI          dw ?
                dw ?
int_SI          dw ?
                dw ?
int_BP          dw ?
                dw ?
                dd ?
int_BX          dw ?
                dw ?
int_DX          dw ?
                dw ?
int_CX          dw ?
                dw ?
int_AX          dw ?
                dw ?
int_flags       dw ?
int_ES          dw ?
int_DS          dw ?
                dw ?
                dw ?
                dw ?
                dw ?
                dw ?
                dw ?
RMI             ENDS

                ELSEIFDEF INT21

RMI             STRUC                   ;Flashtek X32 real-mode int structure
interrupt_num   dw ?
selector_ds     dw ?
selector_es     dw ?
selector_fs     dw ?
selector_gs     dw ?
register_eax    dd ?
register_edx    dd ?
RMI             ENDS

                ENDIF

                ;
                ;Protected-mode descriptor structure
                ;

DT_ENTRY        STRUC
limit_0_15      dw ?
base_0_15       dw ?
base_16_23      db ?
priv            db ?
flgs            db ?
base_24_31      db ?
DT_ENTRY        ENDS


                ;
                ;Protected-mode POPF
                ;

POP_F           MACRO
                LOCAL restore

                push ebp
                mov ebp,esp
                test BYTE PTR [ebp+5],00000010b
                cli
                jz restore
                sti
restore:        pop ebp
                popfd

                ENDM

                ;
                ;Set DX:EBX = interrupt vector #EAX
                ;Also return ECX = real-mode interrupt vector
                ;

GET_VECT        MACRO

                IFDEF DPMI

                push eax
                mov ebx,eax
                mov eax,200h
                int 31h
                shl ecx,16
                mov cx,dx
                pop eax
                push ecx

                mov ah,35h              ;(Rational DOS/4GW)
                push es
                int 21h
                mov dx,es
                pop es

                pop ecx

                ELSEIFDEF INT21

                push es                 ;(Flashtek X32)
                mov cl,al
                push ecx
                mov eax,2502h
                int 21h
                pop ecx
                push ebx
                push es
                mov eax,2503h
                int 21h
                mov ecx,ebx
                pop edx
                pop ebx
                pop es

                ENDIF

                ENDM

                ;
                ;Set reflected interrupt vector #EAX = BX:EDX
                ;

SET_VECT        MACRO

                IFDEF DPMI

                mov ah,25h              ;(Rational DOS/4GW)
                push ds
                mov ds,bx
                int 21h
                pop ds

                ELSEIFDEF INT21

                mov cl,al               ;(Flashtek X32)
                push ds
                mov ds,bx
                mov eax,2506h
                int 21h
                pop ds

                ENDIF

                ENDM

                ;
                ;Set real-mode interrupt vector #EAX CS:IP = EBX
                ;

SET_REAL_VECT   MACRO

                IFDEF DPMI

                mov ecx,ebx
                shr ecx,16
                mov edx,ebx
                and edx,0ffffh
                mov ebx,eax
                mov eax,201h
                int 31h

                ELSEIFDEF INT21

                mov cl,al
                mov eax,2505h
                int 21h

                ENDIF

                ENDM

                ;
                ;Set protected-mode interrupt vector #EAX = BX:EDX
                ;

SET_PROT_VECT   MACRO

                IFDEF DPMI

                mov ah,25h              ;(Rational DOS/4GW)
                push ds
                mov ds,bx
                int 21h
                pop ds

                ELSEIFDEF INT21

                mov cl,al               ;(Flashtek X32)
                push ds
                mov ds,ebx
                mov eax,2504h
                int 21h
                pop ds

                ENDIF

                ENDM

                ;
                ;Set EAX = contents of BIOS data area at real-mode segment
                ;40h, offset BX
                ;

GET_BIOS_DATA   MACRO

                IFDEF DPMI

                IFDEF BORLAND

                push ds
                and ebx,0ffffh          ;(Borland Power Pack)
                push ebx

                mov eax,6
                mov ebx,ds
                int 31h

                shl ecx,16
                mov cx,dx

                pop ebx
                add ebx,400h

                sub ebx,ecx

                mov eax,ds:[ebx]
                pop ds

                ELSE

                push ds
                and ebx,0ffffh          ;(Rational DOS/4GW)
                mov eax,40h
                mov ds,ax
                mov eax,ds:[ebx]
                pop ds

                ENDIF

                ELSEIFDEF INT21

                IFDEF PHARLAP

                push ds                 ;(Phar Lap 386|DOS Extender)
                and ebx,0ffffh
                mov eax,34h
                mov ds,ax
                mov eax,ds:[400h][ebx]
                pop ds

                ELSE

                push ds                 ;(Flashtek X32)
                push ebx
                mov eax,3501h
                int 21h
                mov ds,bx
                mov ecx,400h
                mov eax,3503h
                int 21h
                mov bx,ds
                mov ecx,-1
                mov eax,3505h
                int 21h
                pop ebx
                and ebx,0ffffh
                mov eax,ds:[ebx]
                push eax
                mov bx,ds
                pop eax
                pop ds
                push ds
                push eax
                mov eax,3502h
                int 21h
                pop eax
                pop ds

                ENDIF
                ENDIF

                ENDM

                ;
                ;Prototypes
                ;

TIMER_TYPE      TYPEDEF DWORD

DIG_DRIVER STRUC

tag                 db ?,?,?,?
backgroundtimer     dd ?
quiet               dd ?
n_active_samples    dd ?
master_volume       dd ?
DMA_rate            dd ?
hw_format           dd ?
hw_mode_flags       dd ?
channels_per_sample dd ?
bytes_per_channel   dd ?
channels_per_buffer dd ?
samples_per_buffer  dd ?
playing             dd ?
samples             dd ?
n_samples           dd ?
build_size          dd ?
build_buffer        dd ?
system_data         dd ?,?,?,?,?,?,?,?
buffer_size         dd ?
DMA                 dd ?,?

DIG_DRIVER ENDS

AIL_vmm_lock_range  PROTO C,P1:NEAR PTR,P2:NEAR PTR

AIL_lock        PROTO C
AIL_unlock      PROTO C

AIL_set_timer_period PROTO C,Timer:TIMER_TYPE,uS:DWORD
AIL_release_timer_handle PROTO C,TImer:TIMER_TYPE

AIL_start_timer PROTO C,Timer:TIMER_TYPE
AIL_stop_timer  PROTO C,Timer:TIMER_TYPE


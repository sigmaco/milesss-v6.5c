;############################################################################
;##                                                                        ##
;##  MSSDIGA.ASM                                                           ##
;##                                                                        ##
;##  Miles Sound System                                                    ##
;##                                                                        ##
;##  Digital sound 32-bit flat-model assembly API functions                ##
;##                                                                        ##
;##  Version 1.00 of 20-Apr-94: Initial                                    ##
;##          1.01 of 20-Nov-94: SPLIT copy uses buffer size, not # samples ##
;##                             Added DIG_DUAL_DMA support                 ##
;##          1.02 of 29-Mar-95: Don't clip if only one buffer running      ##
;##          1.03 of  9-Jun-95: Use multiply/shift for 16-bit scaling      ##
;##                             Stereo tracks panned for mono output       ##
;##          1.04 of  7-Nov-95: Master volume can trigger scaling          ##
;##          1.05 of 20-Feb-96: Buffering functionality moved to AILSS.C   ##
;##          2.00 of 15-Aug-98: Removed all but AILSSA_DMA_copy support    ##
;##                             (to allow RAP-10 and GUS legacy support)   ##
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
;##  Contact RAD Game Tools at 801-322-4300 for technical support.         ##
;##                                                                        ##
;############################################################################

                OPTION SCOPED           ;Enable local labels
                .386                    ;Enable 386 instruction set

                IFDEF ZORTECH

                  .MODEL SMALL,C        ;Small 32-bit memory model and C calls
DGROUP            group _TEXT,_DATA     ;(Zortech's BLINK needs this)

                ELSE

                  .MODEL FLAT,C

                ENDIF

DMA_SIGN                 equ 1
DMA_ORDER                equ 2
DMA_POLARITY             equ 4
DMA_SPLIT                equ 8
DMA_STEREO               equ 16
DMA_16                   equ 32
DMA_CLIP                 equ 64

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
                ;Local data
                ;

DGROUP group _DATA1
_DATA1 segment para   public  'DATA'

                ;
                ;Start of locked data
                ;

data_start      LABEL BYTE

operation       dd ?

                ;
                ;End of locked data
                ;

data_end        LABEL BYTE

_DATA1 ENDS

                .CODE

                PUBLIC AILSSA_VMM_lock
                PUBLIC AILSSA_DMA_copy

                ;
                ;Start of locked code
                ;

code_start      LABEL BYTE

;#############################################################################
;##                                                                         ##
;## Macro used to construct AILSSA_DMA_copy() handlers                      ##
;##                                                                         ##
;#############################################################################

MAKE_DC         MACRO Flg
DC_&Flg&        PROC

                ;
                ;Register usage within loop:
                ;
                ;ESI -> source (build) buffer
                ;EDI -> output (DMA) half-buffer
                ;EBX -> output (DMA) half-buffer, 2nd quarter (DMA_SPLIT only)
                ;
                ;ECX  = # samples in output half-buffer (quarter if DMA_SPLIT)
                ;
__fetch:
                IF (OP AND DMA_STEREO)

                  ;
                  ;Stereo output data
                  ;

                  mov eax,[esi]
                  mov edx,[esi+4]

                  sar eax,11
                  sar edx,11

                  add esi,8

                  ;
                  ;Perform clipping, if requested
                  ;

                  IF (OP AND DMA_CLIP)

                    cmp eax,32767
                    jg __L_clip_H
                    cmp eax,-32768
                    jl __L_clip_L
__L_clipped:
                    cmp edx,32767
                    jg __R_clip_H
                    cmp edx,-32768
                    jl __R_clip_L
__R_clipped:
                  ENDIF

                  ;
                  ;AX (AH) = A sample (LR=left, RL=right)
                  ;DX (DH) = B sample (LR=right, RL=left)
                  ;

                  IF ((OP AND DMA_SIGN) EQ 0)

                    xor eax,8000h
                    xor edx,8000h

                  ENDIF

                  IF (OP AND DMA_16)

                    IF (OP AND DMA_POLARITY)

                      xchg al,ah
                      xchg dl,dh

                    ENDIF

                    IF (OP AND DMA_SPLIT)

                      IF (OP AND DMA_ORDER)

                        mov [edi],dx
                        mov [ebx],ax

                      ELSE

                        mov [edi],ax
                        mov [ebx],dx

                      ENDIF

                      add edi,2
                      add ebx,2

                    ELSE

                      IF (OP AND DMA_ORDER)

                        mov [edi],dx
                        mov [edi+2],ax

                      ELSE

                        mov [edi],ax
                        mov [edi+2],dx

                      ENDIF

                      add edi,4

                    ENDIF

                  ELSE

                    IF (OP AND DMA_SPLIT)

                      IF (OP AND DMA_ORDER)

                        mov [edi],dh
                        mov [ebx],ah

                      ELSE

                        mov [edi],ah
                        mov [ebx],dh

                      ENDIF

                      inc edi
                      inc ebx

                    ELSE

                      IF (OP AND DMA_ORDER)

                        mov al,dh
                        mov [edi],ax

                      ELSE

                        mov dl,ah
                        mov [edi],dx

                      ENDIF

                      add edi,2

                    ENDIF

                  ENDIF

                ELSE

                  ;
                  ;Monaural output data
                  ;

                  mov eax,[esi]
                  sar eax,11

                  add esi,4

                  IF (OP AND DMA_CLIP)

                    cmp eax,32767
                    jg __M_clip_H
                    cmp eax,-32768
                    jl __M_clip_L
__M_clipped:
                  ENDIF

                  ;
                  ;AX = mono sample
                  ;

                  IF ((OP AND DMA_SIGN) EQ 0)

                    xor eax,8000h

                  ENDIF

                  IF (OP AND DMA_16)

                    IF (OP AND DMA_POLARITY)

                      xchg al,ah

                    ENDIF

                    mov [edi],ax
                    add edi,2

                  ELSE

                    mov [edi],ah
                    inc edi

                  ENDIF

                ENDIF

                ;
                ;Decrement sample count and continue, else return
                ;

                dec ecx
                jnz __fetch

                ret

                ;
                ;Clipping handlers
                ;

                IF (OP AND DMA_CLIP)

                  IF (OP AND DMA_STEREO)

__L_clip_H:         mov eax,32767
                    jmp __L_clipped
__L_clip_L:         mov eax,-32768
                    jmp __L_clipped

__R_clip_H:         mov edx,32767
                    jmp __R_clipped
__R_clip_L:         mov edx,-32768
                    jmp __R_clipped

                  ELSE

__M_clip_H:         mov eax,32767
                    jmp __M_clipped
__M_clip_L:         mov eax,-32768
                    jmp __M_clipped

                  ENDIF

                ENDIF

DC_&Flg&        ENDP

                ;
                ;Save current assembly location, then insert vector to
                ;primitive at appropriate place in jump table
                ;

                ASM = $

                ORG OFFSET vector_table + ((OP) * SIZE DWORD)
                dd DC_&Flg&

                ORG ASM

                ENDM

;#############################################################################
;##                                                                         ##
;## Primitive vectors generated at beginning of code segment                ##
;##                                                                         ##
;## 1st 128 entries: MAKE_DC procedures for AILSSA_DMA_copy()               ##
;##                                                                         ##
;#############################################################################

PARM_SPACE      equ (128) * SIZE DWORD

vector_table    LABEL DWORD

                ORG ((OFFSET vector_table) + PARM_SPACE)

;#############################################################################
;##                                                                         ##
;## MAKE_DC procedures                                                      ##
;##                                                                         ##
;#############################################################################

                ;
                ;Set up to build all 128 possible DMA copy routines
                ;

OP              = 0
                REPT 128

VALID           = 1

                ;
                ;If not stereo, don't build routines with DMA_SPLIT
                ;

                IF (OP AND DMA_STEREO) EQ 0
                  IF (OP AND DMA_SPLIT)
                    VALID = 0
                  ENDIF
                ENDIF

                ;
                ;If not stereo, don't build routines with DMA_ORDER
                ;

                IF (OP AND DMA_STEREO) EQ 0
                  IF (OP AND DMA_ORDER)
                    VALID = 0
                  ENDIF
                ENDIF

                ;
                ;If not 16-bit audio, don't build routines with DMA_POLARITY
                ;

                IF (OP AND DMA_16) EQ 0
                  IF (OP AND DMA_POLARITY)
                    VALID = 0
                  ENDIF
                ENDIF

                ;
                ;If routine is valid, build it
                ;

                IF VALID

                  MAKE_DC %OP

                ENDIF

OP            = OP + 1
                ENDM

;#############################################################################
;##                                                                         ##
;## Clip and copy build buffer data to real-mode DMA half-buffer            ##
;##                                                                         ##
;## Data in build buffer is 32-bit, signed, LR, LH, mono or stereo          ##
;##                                                                         ##
;## Options: Unsigned DMA buffer / signed DMA buffer   0xxxxxx1 (_SIGN)     ##
;## (0/1)                                                                   ##
;##          LR DMA buffer / RL DMA buffer             0xxxxx1x (_ORDER)    ##
;##                                                                         ##
;##          LH DMA buffer / HL DMA buffer             0xxxx1xx (_POLARITY) ##
;##                                                                         ##
;##          Interleaved DMA buffer / split DMA buffer 0xxx1xxx (_SPLIT)    ##
;##                                                                         ##
;##          Mono DMA buffer / stereo DMA buffer       0xx1xxxx (_STEREO)   ##
;##                                                                         ##
;##          8-bit DMA buffer / 16-bit DMA buffer      0x1xxxxx (_16)       ##
;##                                                                         ##
;##          Do not clip / clip                        01xxxxxx (_CLIP)     ##
;##                                                                         ##
;#############################################################################

AILSSA_DMA_copy PROC C \
                USES ebx esi edi ds es \
                HalfBuff:DWORD,\
                DMA_ptr:DWORD,\
                ModeFlags:DWORD,\
                HWFormat:DWORD,\
                NSamples:DWORD,\
                BuildBuffer:DWORD,\
                SamplesPerBuffer:DWORD,\
                BufferSize:DWORD

                ;
                ;Set DMA_SIGN, DMA_ORDER, DMA_POLARITY, DMA_SPLIT flags
                ;

                mov eax,ModeFlags
                and eax,1111b

                ;
                ;Set DMA_STEREO flag
                ;

                cmp HWFormat,DIG_F_STEREO_8
                je __set_DMA_st
                cmp HWFormat,DIG_F_STEREO_16
                jne __end_DMA_st

__set_DMA_st:   or eax,DMA_STEREO
__end_DMA_st:

                ;
                ;Set DMA_16 flag
                ;

                cmp HWFormat,DIG_F_MONO_16
                je __set_DMA_16
                cmp HWFormat,DIG_F_STEREO_16
                jne __end_DMA_16

__set_DMA_16:   or eax,DMA_16
__end_DMA_16:

                ;
                ;Set DMA_CLIP flag if more than one sample in build buffer
                ;

                cmp NSamples,1
                jbe __end_DMA_cl

                or eax,DMA_CLIP
__end_DMA_cl:

                ;
                ;Write operation flags
                ;

                mov operation,eax

                ;
                ;Set up loop registers
                ;

                mov esi,BuildBuffer
                mov ecx,SamplesPerBuffer

                ;
                ;Determine which DMA half-buffer to fill
                ;
                ;In DUAL mode, fill desired half of both half-buffers
                ;

                test ModeFlags,DIG_DUAL_DMA
                jnz __dual_DMA

                ;
                ;Normal buffer mode: select half-buffer to fill
                ;and handle DIG_PCM_SPLIT for single half-buffer if requested
                ;

                mov eax,[HalfBuff]
                mov ebx,DMA_ptr
                mov edi,[ebx][eax*4]

                test ModeFlags,DIG_PCM_SPLIT
                jz __call_copy

                mov eax,BufferSize
                shr eax,1
                lea ebx,[edi][eax]

                jmp __call_copy

                ;
                ;Dual DMA mode: force DIG_PCM_SPLIT to write across half-
                ;buffer boundary, and select quarter-buffer pair to fill
                ;

__dual_DMA:     or operation,DMA_SPLIT

                mov eax,BufferSize

                mov ebx,DMA_ptr
                mov edi,[ebx][0*4]
                lea ebx,[edi][eax]

                shr eax,1
                mul [HalfBuff]

                add edi,eax
                add ebx,eax

                ;
                ;Call DMA copy loop routine based on desired operation set
                ;

__call_copy:    mov eax,operation

                call cs:vector_table[eax * SIZE DWORD]

__exit:         ret

AILSSA_DMA_copy ENDP

;*****************************************************************************

db 13,10,13,10
db 'Miles Sound System',13,10
db 'Copyright (C) 1991-98 RAD Game Tools, Inc.',13,10,13,10

                ;
                ;End of locked code
                ;

code_end        LABEL BYTE

;#############################################################################
;##                                                                         ##
;## Lock all code and data in AILSSA module                                 ##
;##                                                                         ##
;#############################################################################

AIL_vmm_lock_range  PROTO C,P1:NEAR PTR,P2:NEAR PTR

AILSSA_VMM_lock PROC C \
                USES ebx esi edi ds es

                invoke AIL_vmm_lock_range,OFFSET data_start,OFFSET data_end
                invoke AIL_vmm_lock_range,OFFSET code_start,OFFSET code_end

                ret

AILSSA_VMM_lock ENDP

                END

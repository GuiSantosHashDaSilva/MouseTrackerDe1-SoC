@ REVIEW AND TEST ADDED FUNCTIONS

@ =========================================================================
@ api.s: Library for FPGA Coprocessor Control
@ =========================================================================

@ --- SYSCALLS (ARM Linux EABI) ---

.equ __NR_open, 5
.equ __NR_close, 6
.equ __NR_munmap, 91
.equ __NR_mmap2, 192

@ ===================================================================
@ Data Section (Constants)
@ ===================================================================

.section .data
    dev_mem_path:  .asciz "/dev/mem"
    FPGA_BRIDGE_BASE: .word 0xFF200000
    FPGA_BRIDGE_SPAN: .word 0x00001000 @ 4 KB

    @ --- PIOs OFFSETS (Qsys) ---
    .equ PIO_INSTR_OFS,    0x00
    .equ PIO_ENABLE,       0x10
    .equ PIO_FLAGS_OFS,    0x20
    .equ PIO_DATAOUT_OFS,  0x30
    .equ PIO_MEMSELECT,    0x40

    @ --- INSTRUCTIONS ---
    .equ INSTR_NOP,        0
    .equ INSTR_LOAD,       1
    .equ INSTR_STORE,      2
    .equ INSTR_NHI_ALG,    3
    .equ INSTR_PR_ALG,     4
    .equ INSTR_BA_ALG,     5
    .equ INSTR_NH_ALG,     6
    .equ INSTR_RESET,      7

    @ ======================================================================
    @ BIT MASKS 
    @ =====================================================================

    @ --- CONTROL SIGNALS ---

    .equ ENABLE_BIT_MASK,      1
    .equ SEL_MEM_BIT_MASK,     2 

    @ --- FLAGS ---

    .equ FLAG_DONE_MASK,       1
    .equ FLAG_ERROR_MASK,      2
    .equ FLAG_MAX_ZOOM_MASK,   4
    .equ FLAG_MIN_ZOOM_MASK,   8

    @ --- IMAGE PARAMETERS ---
    .equ IMAGE_WIDTH,      320   @ pixels
    .equ IMAGE_HEIGHT,     240   @ pixels
    .equ IMAGE_SIZE,       76800 @ 76800 Bytes

    @ --- SYNCHRONIZATION PARAMETERS ---

    .equ TIMEOUT_LIMIT,    0x3500
    .equ DELAY_COUNT,      0x1000

@ ===================================================================
@ BSS SECTION (Global Variables)
@ ===================================================================
.section .bss
    .lcomm fd_mem, 4           @ File Descriptor for /dev/mem
    .lcomm lw_bridge_ptr, 4    @ virtual pointer for LW Bridge

@ ===================================================================
@ Text Section
@ ===================================================================
.section .text

@ ===================================================================
@ SUB-ROUTINES "PRIVATE" (Non-visible to C)
@ ===================================================================        

@ _pulse_enable_safe: pulse the ENABLE bit
@ Doesn't affects other flags

_pulse_enable_safe:
    PUSH    {R2, R3, R4}    @ Save registers
    LDR     R4, =lw_bridge_ptr
    LDR     R4, [R4]        @ R4 = ponteiro base

    MOV     R2, #1
    STR     R2, [R4, #PIO_ENABLE]
    MOV     R2, #0
    STR     R2, [R4, #PIO_ENABLE]
    
    POP     {R2, R3, R4}
    BX      LR
    
@ _ASM_Set_Instruction: Internal function
@ ONLY sets the inSTRuction opcode in PIO_INSTR (no pulse or wait)
@ R0 = opcode

_ASM_Set_Instruction:
    PUSH {R0, R4, R8}
    LDR R4, =lw_bridge_ptr
    LDR R4, [R4]
    
    MOV R8, #PIO_INSTR_OFS
    STR R0, [R4, R8]         @ writes opcode (R0) in PIO
    
    POP {R0, R4, R8}
    BX LR                    @ Returns (e.g from NearestNeighBor)

@ _ASM_Get_Flag: Internal function
@ Retorna 1 se a flag (passada em R0) estiver ativa, 0 se não
@ Recebe: R0 = Máscara da Flag (ex: FLAG_DONE_MASK)

_ASM_Get_Flag:
    PUSH {R1, R3, R4}
    LDR R4, =lw_bridge_ptr
    LDR R4, [R4]
    
    MOV R1, #PIO_FLAGS_OFS
    LDR R3, [R4, R1]       @ Read flags PIO
    
    TST R3, R0             @ Tests mask (R0)
    
    MOV R0, #0             @ Set the return value (0 = false)
    MOVNE R0, #1           @ if TST != 0 (NE), switch for 1 (true)
    
    POP {R1, R3, R4}
    BX LR                  @ Returns w R0 = 0 or 1

@ ===================================================================
@ PUBLIC FUNCTIONS (Visible to C)
@ ===================================================================

@ --- API_initialize (void) ---
@ Opens and maps bridge. Returns pointer in R0, or error code on error.

.gloBal API_initialize
.type API_initialize, %function

API_initialize:
    PUSH {R4-R11, LR}
    LDR R0, =dev_mem_path
    MOV R1, #2
    MOV R7, #__NR_open

    SVC 0

    CMP R0, #0
    BLT open_fail

    LDR R1, =fd_mem
    STR R0, [R1]

    MOV R0, #0
    LDR R1, =FPGA_BRIDGE_SPAN
    LDR R1, [R1]
    MOV R2, #3 @ PROT_READ | PROT_WRITE
    MOV R3, #1 @ MAP_SHARED
    LDR R4, =fd_mem
    LDR R4, [R4]
    LDR R5, =FPGA_BRIDGE_BASE
    LDR R5, [R5]
    LSR R5, R5, #12
    MOV R7, #__NR_mmap2

    SVC 0

    CMP R0, #0
    BLT mmap_fail         
    
    LDR R1, =lw_bridge_ptr
    STR R0, [R1]
    POP {R4-R11, PC}        @ RETURNS WITH POINTER IN R0

open_fail:
    MOV R0, #-1             @ RETURNS (-1) ON OPEN ERROR 
    POP {R4-R11, PC}

mmap_fail:
    MOV R0, #-2             @ RETURNS (-2) ON MAP ERROR
    POP {R4-R11, PC}

.size API_initialize, .-API_initialize

@ --- API_close (void) ---
@ Unmap and close /dev/mem

.global API_close
.type API_close, %function

API_close:
    PUSH {R0-R7, LR}
    LDR R0, =lw_bridge_ptr
    LDR R0, [R0]
    LDR R1, =FPGA_BRIDGE_SPAN
    LDR R1, [R1]
    MOV R7, #__NR_munmap
    SVC 0
    
    LDR R0, =fd_mem
    LDR R0, [R0]
    MOV R7, #__NR_close
    SVC 0
    
    POP {R0-R7, PC}
.size API_close, .-API_close

@ --- ASM_Store (R0=address, R1=pixel_data) ---
@ BLOCKING FUNCTION - !
@ Uses _pulse_enable_internal and _wait_for_done_internal

.global ASM_Store
.type ASM_Store, %function

ASM_Store:
    @ load base address and check address validity
    PUSH    {R4-R6, LR}
    LDR     R4, =lw_bridge_ptr
    LDR     R4, [R4]
    CMP     R0, #IMAGE_SIZE
    BHS     .WR_INVALID_ADDRESS

.ASM_WR_PACKET_CONSTRUCTION:
    @ Assembles the instruction packet

    @ OPCODE
    MOV     R2, #INSTR_STORE

    @ ADDRESS
    LSL     R3, R0, #3          
    ORR     R2, R2, R3

    MOV     R3, #1
    @ SELECTION MEMORY BIT
    LSL     R3, R3, #20
    ORR     R2, R2, R3
    @ PIXEL DATA
    LSL     R3, R1, #21
    ORR     R2, R2, R3
    
    STR     R2, [R4, #PIO_INSTR_OFS]
    DMB     sy
    BL      _pulse_enable_safe

    MOV     R5, #TIMEOUT_LIMIT

.WR_POLLING:
    @ polling for DONE flag
    LDR     R2, [R4, #PIO_FLAGS_OFS]
    TST     R2, #FLAG_DONE_MASK
    BNE     .WR_CHECK_ERROR
    SUBS    R5, R5, #1
    BNE     .WR_POLLING
    MOV     R0, #0 @cl
    B       .WR_EXIT

.WR_CHECK_ERROR:
    @ check for ERROR flag
    TST     R2, #FLAG_ERROR_MASK
    BNE     .WR_HW_ERROR
    MOV     R0, #0

    MOV     R5, #DELAY_COUNT

.WR_DELAY:
    @ sync delay
    SUBS    R5, R5, #1
    BNE     .WR_DELAY
    B       .WR_EXIT

.WR_INVALID_ADDRESS:
    MOV     R0, #-1
    B       .WR_EXIT

.WR_HW_ERROR:
    MOV     R0, #-3

.WR_EXIT:
    POP     {R4-R6, PC}
.size ASM_Store, .-ASM_Store

.global ASM_Load
.type ASM_Load, %function

@FIX LOAD INSTRUCTION

ASM_Load:
    PUSH    {R4-R6, LR}
    LDR     R4, =lw_bridge_ptr
    LDR     R4, [R4]
    CMP     R0, #IMAGE_SIZE
    BHS     .RD_INVALID_ADDRESS

.ASM_RD_PACKET_CONSTRUCTION:

    MOV     R0, #INSTR_LOAD
    BL      _ASM_Set_Instruction

    DMB     sy

    BL     _pulse_enable_safe

    LDR     R5, =TIMEOUT_LIMIT

.RD_POLLING:
    LDR     R2, [R4, #PIO_FLAGS_OFS]
    TST     R2, #FLAG_DONE_MASK
    BNE     .RD_SUCCESS

    SUBS    R5, R5, #1
    BNE     .RD_POLLING

    MOV     R0, #-2
    B       .RD_EXIT

.RD_SUCCESS:
    TST    R2, #FLAG_ERROR_MASK
    BNE     .RD_HW_ERROR

    LDR     R0, [R4, #PIO_DATAOUT_OFS]

    MOV     R5, #DELAY_COUNT

.RD_DELAY:
    SUBS    R5, R5, #1
    BNE     .RD_DELAY
    
    B       .RD_EXIT

.RD_INVALID_ADDRESS:
    MOV     R0, #-1
    B       .RD_EXIT

.RD_HW_ERROR:
    MOV     R0, #-3

.RD_EXIT:
    POP     {R4-R6, PC}
.size ASM_Load, .-ASM_Load

@ --- ASM_Refresh (void) ---
@ Sends NOP instruction to refresh internal state

.global ASM_Refresh
.type ASM_Refresh, %function
ASM_Refresh:
    PUSH    {R2, R4, LR}
    
    LDR     R4, =lw_bridge_ptr
    LDR     R4, [R4]

    MOV     R2, #INSTR_NOP
    STR     R2, [R4, #PIO_INSTR_OFS]

    BL      _pulse_enable_safe
    
    POP     {R2, R4, PC}
.size ASM_Refresh, .-ASM_Refresh

@ --- ASM_Pulse_Enable (void) --- 
@ Pulse ENABLE bit

.global ASM_Pulse_Enable
.type ASM_Pulse_Enable, %function

ASM_Pulse_Enable:
    PUSH {LR}
    BL _pulse_enable_safe @ Calls internal function
    POP {PC}
.size ASM_Pulse_Enable, .-ASM_Pulse_Enable

@ ===================================================================
@ ALGORITHM BLOCKS - Non-blocking functions
@ ONLY DEFINE THE INSTRUCTIONS - PULSE THE ENABLE WITH ASM_Pulse_Enable TO RUN
@ ===================================================================

@ --- NearestNeighBor (void) ---
.global NearestNeighbor
.type NearestNeighbor, %function

NearestNeighbor:
    PUSH {LR}
    MOV R0, #INSTR_NHI_ALG       @ opcode
    BL _ASM_Set_Instruction      @ calls internal function
    POP {PC}                     @ return (no pulse, no wait)
.size NearestNeighbor, .-NearestNeighbor

@ --- ASM_SetPrimaryMemory (void) ---
@ Define o bit SEL_MEM para 0
.global ASM_SetPrimaryMemory
.type ASM_SetPrimaryMemory, %function
ASM_SetPrimaryMemory:
    PUSH    {r0, r4, lr}
    LDR     r4, =lw_bridge_ptr
    LDR     r4, [r4]            

    MOV     r0, #0
    STR     r0, [r4, #PIO_MEMSELECT] @ Writes 0 to PIO_MEMSELECT
    
    POP     {r0, r4, pc}
.size ASM_SetPrimaryMemory, .-ASM_SetPrimaryMemory

@ --- ASM_SetSecondaryMemory (void) ---
@ Define o bit SEL_MEM para 1
.global ASM_SetSecondaryMemory
.type ASM_SetSecondaryMemory, %function
ASM_SetSecondaryMemory:
    PUSH    {r0, r4, lr}
    LDR     r4, =lw_bridge_ptr
    LDR     r4, [r4]            

    MOV     r0, #1
    STR     r0, [r4, #PIO_MEMSELECT] @ Writes 1 to PIO_MEMSELECT
    
    POP     {r0, r4, pc}
.size ASM_SetSecondaryMemory, .-ASM_SetSecondaryMemory

@ --- PixelReplication (void) ---
.global PixelReplication
.type PixelReplication, %function

PixelReplication:
    PUSH {LR}
    MOV R0, #INSTR_PR_ALG
    BL _ASM_Set_Instruction
    POP {PC}
.size PixelReplication, .-PixelReplication

@ --- Decimation (void) ---
.global Decimation
.type Decimation, %function

Decimation:
    PUSH {LR}
    MOV R0, #INSTR_NH_ALG
    BL _ASM_Set_Instruction
    POP {PC}
.size Decimation, .-Decimation

@ --- BlockAveraging (void) ---
.global BlockAveraging
.type BlockAveraging, %function

BlockAveraging:
    PUSH {LR}
    MOV R0, #INSTR_BA_ALG
    BL _ASM_Set_Instruction
    POP {PC}
.size BlockAveraging, .-BlockAveraging

@ --- RESET FUNCTION ---
@ --- ASM_Run_Reset (void) ---
@ Resets the coprocessor internal state

.global ASM_Reset
.type ASM_Reset, %function

ASM_Reset:
    PUSH {LR}
    MOV R0, #INSTR_RESET
    BL _ASM_Set_Instruction
    POP {PC}
.size ASM_Reset, .-ASM_Reset

@ ===================================================================
@ GET FLAG FUNCTIONS
@ RETURNS 1 (true) or 0 (false)
@ ===================================================================

@ FLAG DONE gets high when an operation is complete

.global ASM_Get_Flag_Done
.type ASM_Get_Flag_Done, %function

ASM_Get_Flag_Done:
    PUSH {LR}
    MOV R0, #FLAG_DONE_MASK
    BL _ASM_Get_Flag
    POP {PC}
.size ASM_Get_Flag_Done, .-ASM_Get_Flag_Done

.global ASM_Get_Flag_Error
.type ASM_Get_Flag_Error, %function

@ FLAG ERROR gets high when an error occurs during an operation

ASM_Get_Flag_Error:
    PUSH {LR}
    MOV R0, #FLAG_ERROR_MASK
    BL _ASM_Get_Flag
    POP {PC}
.size ASM_Get_Flag_Error, .-ASM_Get_Flag_Error

.global ASM_Get_Flag_Max_Zoom
.type ASM_Get_Flag_Max_Zoom, %function

@ FLAG MAX ZOOM gets high when maximum zoom level is reached

ASM_Get_Flag_Max_Zoom:
    PUSH {LR}
    MOV R0, #FLAG_MAX_ZOOM_MASK
    BL _ASM_Get_Flag
    POP {PC}
.size ASM_Get_Flag_Max_Zoom, .-ASM_Get_Flag_Max_Zoom

.global ASM_Get_Flag_Min_Zoom
.type ASM_Get_Flag_Min_Zoom, %function

@ FLAG MIN ZOOM gets high when minimum zoom level is reached

ASM_Get_Flag_Min_Zoom:
    PUSH {LR}
    MOV R0, #FLAG_MIN_ZOOM_MASK
    BL _ASM_Get_Flag
    POP {PC}
.size ASM_Get_Flag_Min_Zoom, .-ASM_Get_Flag_Min_Zoom

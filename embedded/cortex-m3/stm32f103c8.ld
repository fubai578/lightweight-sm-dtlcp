/* stm32f103c8.ld — STM32F103C8T6 链接脚本（**64KB Flash / 20KB RAM**）
 *
 * 器件实际资源（地阔星 F103C8T6，Cortex-M3 @72MHz）：
 *   Flash 64 KB @ 0x08000000   RAM 20 KB @ 0x20000000
 *   ⚠ 常见"8倍片"假芯片只有 32KB Flash / 20KB RAM，见 docs/08 的验货提示。
 *
 * 本脚本刻意**不预留大栈**（无 _Min_Stack_Size），因为栈深由 scripts/stack_depth.py
 * 单独测算（M3 ABI 下最大调用路径约 3.4 KB），任务栈建议 ≥5 KB 见 docs/04 §5.1。
 */
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
  .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH
  _sidata = LOADADDR(.data);
  .data : { _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT> FLASH
  .bss : { _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
  ._user_heap_stack : { . = ALIGN(8); } > RAM
}
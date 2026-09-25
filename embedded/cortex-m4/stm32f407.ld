/* stm32f407.ld — 最小链接脚本（STM32F407: 1MB Flash, 128KB RAM）
 * 完整工程可替换为芯片厂商/CubeMX 生成的脚本。
 */
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
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

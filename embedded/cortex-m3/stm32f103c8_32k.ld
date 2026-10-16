/*
 * stm32f103c8_32k.ld — STM32F103C8T6 **32KB Flash 约束**链接脚本
 *
 * 用途：**证明"适配资源受限终端"**。把 FLASH LENGTH 改成 32K 后若仍能链接成功，
 * 即证明裁剪配置可装进 32KB Flash 器件 —— 这正是赛题「资源受限」的核心论据，
 * 且**完全免费、可复现、不需要买小 Flash 的板子**（见 docs/05 §3.2）。
 *
 * RAM 也收紧到 16K（给 DTLCP 上下文留 4K），用于提前暴露"能不能装得下"的问题。
 */
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 32K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 16K
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
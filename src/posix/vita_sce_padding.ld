/* Reserve room for vita-elf-create metadata between load segments. */
SECTIONS { . = ALIGN(0x1000) + 0x1000; }
INSERT BEFORE .init_array;

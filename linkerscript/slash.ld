SECTIONS
{
  .rodata.slash :
  {
    __start_slash = .;
    KEEP(*(SORT_BY_NAME(.slash.*)));
    __stop_slash = .;
  }
}
INSERT AFTER .rodata;

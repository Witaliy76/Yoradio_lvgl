# IDF 5.5.5 assigns 64-bit exFAT sizes to 32-bit st_size. Reject overflow
# before that assignment so neither stat path can report a truncated size.
# IDF 5.5.5 сужает размер exFAT до 32 бит. Отвергаем размер до присваивания.
set(_vfs_source "${_fatfs_dir}/vfs/vfs_fat.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_vfs_source}")
file(READ "${_vfs_source}" _vfs)

function(_yoradio_vfs_replace old new)
    string(FIND "${_vfs}" "${old}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "Review FatFs VFS size guard for this SDK: ${old}")
    endif()
    string(REPLACE "${old}" "${new}" _vfs "${_vfs}")
    set(_vfs "${_vfs}" PARENT_SCOPE)
endfunction()

_yoradio_vfs_replace("    st->st_size = f_size(file);"
    "    if (f_size(file) > INT32_MAX) { errno = EOVERFLOW; return -1; }\n    st->st_size = f_size(file);")
_yoradio_vfs_replace("static void update_stat_struct(struct stat *st, FILINFO *info)\n{"
    "static int update_stat_struct(struct stat *st, FILINFO *info)\n{\n    if (info->fsize > INT32_MAX) { errno = EOVERFLOW; return -1; }")
_yoradio_vfs_replace("    st->st_ctime = 0;\n}\n\nstatic int vfs_fat_stat"
    "    st->st_ctime = 0;\n    return 0;\n}\n\nstatic int vfs_fat_stat")
_yoradio_vfs_replace("        update_stat_struct(st, &fat_ctx->cached_fileinfo.fileinfo);\n        memset(&fat_ctx->cached_fileinfo, 0 ,sizeof(FILINFO));\n        return 0;"
    "        int ret = update_stat_struct(st, &fat_ctx->cached_fileinfo.fileinfo);\n        memset(&fat_ctx->cached_fileinfo, 0 ,sizeof(FILINFO));\n        return ret;")
_yoradio_vfs_replace("    update_stat_struct(st, &info);\n    return 0;"
    "    return update_stat_struct(st, &info);")
configure_file("${CMAKE_CURRENT_LIST_DIR}/vfs_fat.c.in"
    "${_lfn_mirror}/vfs_fat.c" @ONLY)

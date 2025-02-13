# Copyright (c) 2006, 2014, Oracle and/or its affiliates
# Copyright (c) 2009, 2018, MariaDB Corporation
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1335 USA

INCLUDE_DIRECTORIES(${ZLIB_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/mysys)

SET(MYSYS_SOURCES  array.c charset-def.c charset.c checksum.c my_default.c
                get_password.c
				errors.c hash.c list.c
                                mf_cache.c mf_dirname.c mf_fn_ext.c
				mf_format.c mf_getdate.c mf_iocache.c mf_iocache2.c mf_keycache.c 
				mf_keycaches.c mf_loadpath.c mf_pack.c mf_path.c mf_qsort.c mf_qsort2.c
				mf_radix.c mf_same.c mf_sort.c mf_soundex.c mf_arr_appstr.c mf_tempdir.c
				mf_tempfile.c mf_unixpath.c mf_wcomp.c mulalloc.c my_access.c
				my_alloc.c my_bit.c my_bitmap.c my_chsize.c
				my_compress.c my_copy.c  my_create.c my_delete.c
				my_div.c my_error.c my_file.c my_fopen.c my_fstream.c 
				my_gethwaddr.c my_getopt.c my_getsystime.c my_getwd.c my_compare.c my_init.c
				my_lib.c my_lock.c my_malloc.c my_mess.c
				my_mkdir.c my_mmap.c my_once.c my_open.c my_pread.c my_pthread.c
				my_quick.c my_read.c my_redel.c my_rename.c my_seek.c my_sleep.c
				my_static.c my_symlink.c my_symlink2.c my_sync.c my_thr_init.c 
				my_basename.c
				my_write.c ptr_cmp.c queues.c stacktrace.c
				string.c thr_alarm.c thr_lock.c thr_mutex.c
				thr_rwlock.c thr_timer.c
                                tree.c typelib.c base64.c my_memmem.c
                                my_getpagesize.c
                                guess_malloc_library.c
				lf_alloc-pin.c lf_dynarray.c lf_hash.c
                                safemalloc.c my_new.cc
				my_getncpus.c my_safehash.c my_chmod.c my_rnd.c
                                my_uuid.c wqueue.c waiting_threads.c ma_dyncol.c ../sql-common/my_time.c
				my_rdtsc.c my_context.c psi_noop.c
                                my_atomic_writes.c my_cpu.c my_likely.c
                                file_logger.c my_dlerror.c)

IF (WIN32)
  SET (MYSYS_SOURCES ${MYSYS_SOURCES}
    my_winthread.c
    my_wincond.c
    my_winerr.c
    my_winfile.c
    my_conio.c
    my_minidump.cc
    my_win_popen.cc)
ENDIF()

IF(UNIX)
 SET (MYSYS_SOURCES ${MYSYS_SOURCES} my_addr_resolve.c my_setuser.c)
ENDIF()

IF(HAVE_ALARM)
 SET(MYSYS_SOURCES ${MYSYS_SOURCES} my_alarm.c)
ENDIF()

IF(HAVE_LINUX_LARGE_PAGES)
 SET(MYSYS_SOURCES ${MYSYS_SOURCES} my_largepage.c)
ENDIF()

IF(HAVE_MLOCK)
 SET(MYSYS_SOURCES ${MYSYS_SOURCES} my_lockmem.c)
ENDIF()

ADD_CONVENIENCE_LIBRARY(mysys ${MYSYS_SOURCES})
TARGET_LINK_LIBRARIES(mysys dbug strings ${ZLIB_LIBRARY}
 ${LIBNSL} ${LIBM} ${LIBRT} ${CMAKE_DL_LIBS} ${LIBSOCKET} ${LIBEXECINFO} ${CRC32_LIBRARY})
DTRACE_INSTRUMENT(mysys)

IF (HAVE_GCC_C11_ATOMICS_WITH_LIBATOMIC)
  TARGET_LINK_LIBRARIES(mysys atomic)
ENDIF()

IF(HAVE_BFD_H)
  TARGET_LINK_LIBRARIES(mysys bfd)  
ENDIF(HAVE_BFD_H)

IF (WIN32)
  TARGET_LINK_LIBRARIES(mysys iphlpapi dbghelp)
ENDIF(WIN32)

# Need explicit pthread for gcc -fsanitize=address
IF(CMAKE_USE_PTHREADS_INIT AND CMAKE_C_FLAGS MATCHES "-fsanitize=")
  TARGET_LINK_LIBRARIES(mysys pthread)
ENDIF()

ADD_EXECUTABLE(thr_lock thr_lock.c)
TARGET_LINK_LIBRARIES(thr_lock mysys)
SET_TARGET_PROPERTIES(thr_lock PROPERTIES COMPILE_FLAGS "-DMAIN")

ADD_EXECUTABLE(thr_timer thr_timer.c)
TARGET_LINK_LIBRARIES(thr_timer mysys)
SET_TARGET_PROPERTIES(thr_timer PROPERTIES COMPILE_FLAGS "-DMAIN")

ADD_EXECUTABLE(test_hash hash.c)
TARGET_LINK_LIBRARIES(test_hash mysys)
SET_TARGET_PROPERTIES(test_hash PROPERTIES COMPILE_FLAGS "-DMAIN")

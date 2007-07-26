! This file created from test/mpi/f77/datatype/packef.f with f77tof90
! -*- Mode: Fortran; -*- 
!
!  (C) 2003 by Argonne National Laboratory.
!      See COPYRIGHT in top-level directory.
!
       program main
       use mpi
       integer ierr, errs
       integer inbuf(10), ioutbuf(10), inbuf2(10), ioutbuf2(10)
       integer i, insize, rsize, csize, insize2
       character*(16) cbuf, coutbuf
       double precision rbuf(10), routbuf(10)
       integer packbuf(1000), pbufsize, intsize
       integer max_asizev
       parameter (max_asizev = 2)
       integer (kind=MPI_ADDRESS_KIND) asize, asizev(max_asizev)


       errs = 0
       call mtest_init( ierr )

       call mpi_type_size( MPI_INTEGER, intsize, ierr )
       pbufsize = 1000 * intsize

       call mpi_pack_external_size( 'external32', 10, MPI_INTEGER,  &
      &                              asize, ierr ) 
       if (asize .ne. 10 * 4) then
          errs = errs + 1
          print *, 'Expected 40 for size of 10 external32 integers', &
      &       ', got ', asize
       endif
       call mpi_pack_external_size( 'external32', 10, MPI_LOGICAL,  &
      &                              asize, ierr ) 
       if (asize .ne. 10 * 4) then
          errs = errs + 1
          print *, 'Expected 40 for size of 10 external32 logicals', &
      &       ', got ', asize
       endif
       call mpi_pack_external_size( 'external32', 10, MPI_CHARACTER,  &
      &                              asize, ierr ) 
       if (asize .ne. 10 * 1) then
          errs = errs + 1
          print *, 'Expected 10 for size of 10 external32 characters', &
      &       ', got ', asize
       endif
       
       call mpi_pack_external_size( 'external32', 3, MPI_INTEGER2, &
      &                              asize, ierr )
       if (asize .ne. 3 * 2) then
          errs = errs + 1
          print *, 'Expected 6 for size of 3 external32 INTEGER*2', &
      &       ', got ', asize
       endif
       call mpi_pack_external_size( 'external32', 3, MPI_INTEGER4, &
      &                              asize, ierr )
       if (asize .ne. 3 * 4) then
          errs = errs + 1
          print *, 'Expected 12 for size of 3 external32 INTEGER*4', &
      &       ', got ', asize
       endif
       call mpi_pack_external_size( 'external32', 3, MPI_REAL4, &
      &                              asize, ierr )
       if (asize .ne. 3 * 4) then
          errs = errs + 1
          print *, 'Expected 12 for size of 3 external32 REAL*4', &
      &       ', got ', asize
       endif
       call mpi_pack_external_size( 'external32', 3, MPI_REAL8, &
      &                              asize, ierr )
       if (asize .ne. 3 * 8) then
          errs = errs + 1
          print *, 'Expected 24 for size of 3 external32 REAL*8', &
      &       ', got ', asize
       endif
       if (MPI_INTEGER1 .ne. MPI_DATATYPE_NULL) then
          call mpi_pack_external_size( 'external32', 3, MPI_INTEGER1, &
      &                              asize, ierr )
          if (asize .ne. 3 * 1) then
             errs = errs + 1
             print *, 'Expected 3 for size of 3 external32 INTEGER*1', &
      &            ', got ', asize
          endif
       endif
       if (MPI_INTEGER8 .ne. MPI_DATATYPE_NULL) then
          call mpi_pack_external_size( 'external32', 3, MPI_INTEGER8, &
      &                              asize, ierr )
          if (asize .ne. 3 * 8) then
             errs = errs + 1
             print *, 'Expected 24 for size of 3 external32 INTEGER*8', &
      &            ', got ', asize
          endif
       endif

!
! Initialize values
!
       insize = 10
       do i=1, insize
          inbuf(i) = i
       enddo
       rsize = 3
       do i=1, rsize
          rbuf(i) = 1000.0 * i
       enddo
       cbuf  = 'This is a string'
       csize = 16
       insize2 = 7
       do i=1, insize2
          inbuf2(i) = 5000-i
       enddo
!
       asizev(1) = pbufsize
       asizev(2) = 0
       call mpi_pack_external( 'external32', inbuf, insize, MPI_INTEGER, &
      &               packbuf, asizev(1), asizev(2), ierr )
       call mpi_pack_external( 'external32', rbuf, rsize,  &
      &               MPI_DOUBLE_PRECISION, packbuf, asizev(1),  &
      &               asizev(2), ierr )
       call mpi_pack_external( 'external32', cbuf, csize,  &
      &               MPI_CHARACTER, packbuf, asizev(1),  &
      &               asizev(2), ierr )
       call mpi_pack_external( 'external32', inbuf2, insize2,  &
      &               MPI_INTEGER, &
      &               packbuf, asizev(1), asizev(2), ierr )
!
! We could try sending this with MPI_BYTE...
       asizev(2) = 0
       call mpi_unpack_external( 'external32', packbuf, asizev(1), &
      &  asizev(2), ioutbuf, insize, MPI_INTEGER, ierr )
       call mpi_unpack_external( 'external32', packbuf, asizev(1), &
      &  asizev(2), routbuf, rsize, MPI_DOUBLE_PRECISION, ierr )
       call mpi_unpack_external( 'external32', packbuf, asizev(1), &
      &  asizev(2), coutbuf, csize, MPI_CHARACTER, ierr )
       call mpi_unpack_external( 'external32', packbuf, asizev(1), &
      &  asizev(2), ioutbuf2, insize2, MPI_INTEGER, ierr )
!
! Now, test the values
!
       do i=1, insize
          if (ioutbuf(i) .ne. i) then
             errs = errs + 1
             print *, 'ioutbuf(',i,') = ', ioutbuf(i)
          endif
       enddo
       do i=1, rsize
          if (routbuf(i) .ne. 1000.0 * i) then
             errs = errs + 1
             print *, 'routbuf(',i,') = ', routbuf(i)
          endif
       enddo
       if (coutbuf(1:csize) .ne. 'This is a string') then
          errs = errs + 1
          print *, 'coutbuf = ', coutbuf(1:csize)
       endif
       do i=1, insize2
          if (ioutbuf2(i) .ne. 5000-i) then
             errs = errs + 1
             print *, 'ioutbuf2(',i,') = ', ioutbuf2(i)
          endif
       enddo
!
       call mtest_finalize( errs )
       call mpi_finalize( ierr )
       end

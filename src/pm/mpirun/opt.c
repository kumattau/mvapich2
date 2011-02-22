/* Copyright (c) 2003-2011, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level MVAPICH2 directory.
 *
 */
#include <mpirunconf.h>

#ifdef CR_AGGRE

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>

#include "crfs.h"
#include "debug.h"

#define	MAX_PATH_LEN	(128)	// length of mount-point
#define MAX_CMD_LEN	(MAX_PATH_LEN*2)

char	crfs_wa_real[MAX_PATH_LEN];
char	crfs_wa_mnt[MAX_PATH_LEN];

char	crfile_basename[MAX_PATH_LEN]; // base filename of CR files

char	crfs_mig_real[MAX_PATH_LEN];
char	crfs_mig_mnt[MAX_PATH_LEN];

char    crfs_mig_filename[MAX_PATH_LEN]; // like:  /tmp/cr-<sessionid>/mig/myfile

char crfs_sessionid[MAX_PATH_LEN];

extern int crfs_mode;
extern int mig_role;


static long parse_value_string(char* msg);
int crfs_wa_pid = 0;

int crfs_start_mig(char* tgt)
{
    int ret;    
    ret = lsetxattr(crfs_mig_mnt, "migration.tgt", tgt, strlen(tgt), 0); 
    
    int run=1;
    ret = lsetxattr(crfs_mig_mnt, "migration.state", &run, sizeof(int), 0); 
    
    dbg("***   have started mig to %s\n", tgt);
    return ret;
}


int crfs_stop_mig()
{
    int ret;    

    int run=0;
    ret = lsetxattr(crfs_mig_mnt, "migration.state", &run, sizeof(int), 0); 
    
    dbg("****  have stopped mig\n");
    return ret;
}

// ckptfile is:  ${real-dir}/filename-base. 
// Need to extract "${real-dir}" and "file-basename" from it
int	start_crfs_wa(char* sessionid, char* realdir)
{
	char	cmd[MAX_CMD_LEN];	
    	
	memset( crfs_wa_real, 0, MAX_PATH_LEN );	
	memset( crfs_wa_mnt, 0, MAX_PATH_LEN );
	memset( cmd, 0, MAX_CMD_LEN );
	
	strncpy( crfs_wa_real, realdir, MAX_PATH_LEN );
	snprintf( cmd, MAX_CMD_LEN, "mkdir -p %s", crfs_wa_real);
	system(cmd);
	
	snprintf( crfs_wa_mnt, MAX_PATH_LEN, "/tmp/cr-%s/wa/", sessionid );

	snprintf( cmd, MAX_CMD_LEN, "mkdir -p %s", crfs_wa_mnt);
	system(cmd);
    

	int	pfd[2];
	char	ch;
	
	int argc = 0;
	char *argv[10];
	int fg = 0;
	
	crfs_mode = MODE_WRITEAGGRE;
	mig_role = ROLE_INVAL;
	
	argv[0] = "crfs-wa";
	argv[1] = crfs_wa_real;
	argv[2] = crfs_wa_mnt;
	argv[3] = "-obig_writes"; //NULL; //"-odirect_io"; //"-obig_writes";
    argv[4] = NULL;
    argc = 4;
	if( fg ){
		argv[argc] = "-f"; 
		argc++;
		argv[argc] = NULL; 
	}

    dbg("real-dir=%s, mnt=%s\n", argv[1], argv[2] );

	if( pipe(pfd)!= 0 )
	{
		perror("Fail to create pipe...\n");
		return -1;
	}
	
	pid_t pid = fork();
    	
	if(pid==0) // in child proc
	{
        extern void* crfs_main(int pfd, int argc, char** argv);
        close(pfd[0]); // close the read-end
		crfs_main(pfd[1], argc, argv);
		dbg("Child-proc: will exit now...\n");
		exit(0);
	}
	else if( pid < 0 ) // error!! 
	{ 
		perror("fail to fork...\n");
		return -1;
    }	
    crfs_wa_pid = pid;	
	/// pid>0: in parent proc
	close(pfd[1]); // close the write-end
	dbg("parent proc waits...\n\n");
	read( pfd[0], &ch, 1 ); // wait for a sig
	dbg("*****   has got a char ==  %c\n", ch);
	close(pfd[0]);
    if( ch!='0'){
        stop_crfs_wa();
        return -1;
    }
	return 0;
}


/// src_tgt::  0=at src, 1=srv tgt
int start_crfs_mig( char *sessionid, int src_tgt )
{
	char	cmd[MAX_CMD_LEN];
	
	char	*realdir; //[MAX_PATH_LEN]; // real-path where to 
	char	*ckpt_filename; //[MAX_PATH_LEN];

    if( src_tgt!=1 && src_tgt!=2 ){
        err("Incorrect param: src_tgt=%d\n", src_tgt);
        return -1;
    }	
	//realdir = ckptfile;
	
	memset( crfs_mig_real, 0, MAX_PATH_LEN );	
	memset( crfs_mig_mnt, 0, MAX_PATH_LEN );
	memset( cmd, 0, MAX_CMD_LEN );
	
	snprintf( crfs_mig_real, MAX_PATH_LEN, "/tmp/cr-%s/", sessionid );	
	snprintf( cmd, MAX_CMD_LEN, "mkdir -p %s", crfs_mig_real);
	system(cmd);
	
	snprintf( crfs_mig_mnt, MAX_PATH_LEN,  "/tmp/cr-%s/mig/", sessionid );	
	snprintf( cmd, MAX_CMD_LEN, "mkdir -p %s", crfs_mig_mnt);
	system(cmd);
	
	int	pfd[2];
	char	ch;
	
	int argc = 0;
	char *argv[10]; //
	//{ "crfs-wa",  "/tmp/ckpt", "/tmp/mnt", "-f", "-odirect_io", NULL };

	if(src_tgt==1) // I'm mig-source
	{
		crfs_mode = MODE_WRITEAGGRE;
	 	mig_role = ROLE_MIG_SRC;
 	}
	else if( src_tgt==2)// at target side
	{
		crfs_mode = MODE_MIG;
		mig_role = ROLE_MIG_TGT;
	}
    int fg = 0;


	argv[0] = "crfs-mig";
	argv[1] = crfs_mig_real;
	argv[2] = crfs_mig_mnt;
    argv[3] = "-osync_read";
	argv[4] = NULL; //"-odirect_io"; //"-obig_writes";
    argv[4] = 0;
    argc = 4;
    if( fg ){
        argv[argc++] = "-f";
        argv[argc]=NULL;
    }
	
	if( pipe(pfd)!= 0 )
	{
		perror("Fail to create pipe...\n");
		return -1;
	}
	
	pid_t pid = fork();
	
	if(pid==0) // in child proc
	{
		extern void* crfs_main(int pfd, int argc, char** argv);
		close(pfd[0]); // close the read-end
		crfs_main(pfd[1], argc, argv);
		dbg("Child-proc: will exit now...\n");
		exit(0);
	}
	else if( pid < 0 ) // error!! 
	{ 
		perror("fail to fork...\n");
		return -1;
	}	
	
	/// pid>0: in parent proc
	close(pfd[1]); // close the write-end
	dbg("parent proc waits...\n\n");
	read( pfd[0], &ch, 1 ); // wait for a sig
	dbg("has got a char: %c\n", ch);
	close(pfd[0]);
    if( ch!='0'){
        stop_crfs_mig();
        return -1;
    }

	return 0;

}

// the ckptfile is: ${dir}/filename. Parse this string to 
// extract ${dir} and filename.
int	parse_ckptname(char* ckptfile, char* out_dir, char* out_file)
{
	int i;
	
	int len = strlen(ckptfile);
	char *p;
	
	p = ckptfile+len-1;
	while(len)
	{
		if(*p == '/') // find the last "/", any before it is the dir-path
		{
			break;
		}
		len--;
		p--;
	}
	
	if( len <= 0 ) // something is wrong. ill-formated input ckptfile name
	{
		err("incorrect ckpt name: %s\n", ckptfile);
		return -1;
	}
	
	strncpy(out_dir, ckptfile, len );
	out_dir[len] = 0;
	
	i = strlen(ckptfile)-len;
	strncpy(out_file, p+1, i );	
	out_file[i] = 0;
	
	return 0;
}

///
static int check_dir(char* dirpath)
{
    int rv;
    struct stat sbuf;
    
    rv = stat(dirpath, &sbuf);
    if( rv  ){
        if( errno == ENOENT ){
            rv = mkdir(dirpath, 0755);
            dbg("create dir: %s ret %d\n", dirpath, rv);
            return rv;
        }
        err("Fail to open dir:  %s\n", dirpath);
        return rv;
    }

    if( !S_ISDIR(sbuf.st_mode) ){
        err("path: %s isn't a dir!!\n", dirpath);        
        rv = -1;
    }
    
    return rv;
}

extern long cli_rdmabuf_size, srv_rdmabuf_size;
extern int rdmaslot_size;

static int has_mig_fs = 0;

int		start_crfs(char* sessionid, char* fullpath, int mig )
{
	int rv;	
	char	realdir[256];
	
	if( parse_ckptname(fullpath, realdir, crfile_basename) != 0 )
	{
		printf("%s: Error at parsing ckfile: %s\n", __func__, fullpath);
		return -1;
	}
    if(  check_dir(realdir) != 0 ){
        return -1;
    }
    strcpy(crfs_sessionid, sessionid);
    dbg("parse fullpath: %s to %s : %s \n", fullpath, realdir, crfile_basename ); 

    /// now, init the bufpool & chunk-size
    long val;
    char    *p;
    p = getenv("MV2_CKPT_AGGREGATION_BUFPOOL_SIZE");
    if( p && (val=parse_value_string(p))>0 ){
        srv_rdmabuf_size = cli_rdmabuf_size = val;
    }
    p = getenv("MV2_CKPT_AGGREGATION_CHUNK_SIZE");
    if( p && (val=parse_value_string(p))>0 ){
        rdmaslot_size = (int)val;
    }
    dbg("cli_rdmabuf_size=%ld, srv_rdmabuf_size=%ld, slot-size=%d\n", 
        cli_rdmabuf_size, srv_rdmabuf_size, rdmaslot_size );

	rv = start_crfs_wa( sessionid, realdir);
	dbg("[mt_%d]: Start WA ret %d\n", mt_id,rv);
    
    if( rv!= 0 ){
        err("Fail to start CR-aggregation...\n");
        return rv;
    } 
    
    // this "fullpath" is used in aggre-based ckpt
    snprintf(fullpath, MAX_PATH_LEN, "%s%s", crfs_wa_mnt, crfile_basename);
    dbg("Now, def cktp file=%s\n", fullpath );
    dbg("---------  crfs-mig func=%d\n", mig );

    has_mig_fs = 0;
    if( mig > 0 ){
    	rv = start_crfs_mig( sessionid, mig );
	    dbg("[mt_%d]: Start Mig ret %d\n", mt_id, rv);
        if(rv==0 ){
            has_mig_fs = mig;
            // this mig_filename is the filename used in aggre-based migration
            snprintf(crfs_mig_filename, MAX_PATH_LEN, "%s%s", crfs_mig_mnt, crfile_basename);
        }
        else{
            err("Fail to start Aggre-for-Migration...\n");
            stop_crfs_wa();
            return rv;
        }
	}

	return rv;
}

// string format is:   xxx<Kk/Mm/Gg>
static long parse_value_string(char* msg)
{
    int len;
    if(!msg || (len=strlen(msg))<1)
        return -1L;
    char c = msg[len-1];

    unsigned long val;
    unsigned long unit;
    switch(c){
        case 'k':
        case 'K':
            unit = 1024;
            break;
        case 'm':
        case 'M':
            unit = 1024*1024;
            break;
        case 'g':
        case 'G':
            unit = 1024UL*1024*1024;
            break;
        default:
            unit = 1;
            break;
    }
    
    val = atol(msg) * unit;

    return val;  

}


int stop_crfs()
{
	char	path[MAX_PATH_LEN];
    extern int mt_id; 
    int rv;
    char    cmd[MAX_CMD_LEN];

    snprintf(cmd, MAX_CMD_LEN, "fusermount -u %s > /dev/null 2>&1", crfs_wa_mnt );
    dbg("[mt_%d]: will stop crfs-wa:  %s\n", mt_id, cmd);
    system(cmd);

    if( crfs_wa_pid > 0 ){
        rv = kill( crfs_wa_pid, SIGTERM);
        dbg("kill with SIGTERM ret=%d\n", rv);
        usleep(100000); // wait for CRFS to terminate
        rv = kill( crfs_wa_pid, SIGINT );
        dbg("kill with SIGINT ret=%d\n", rv);
    }
    //snprintf(path, MAX_PATH_LEN, "/tmp/cr-%s/", crfs_sessionid);
    //rv = rmdir(path);
	return 0;
}

int stop_crfs_mig()
{
    return stop_crfs();
}


int	stop_crfs_wa()
{
    return stop_crfs();
}



#endif

#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <blkid.h>
#include <stdarg.h>
#include <getopt.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <scsi/sg.h>
#include <sys/ioctl.h>
#include <src/config.h>
#include <sys/inotify.h>
#include <libblkid.h>

#define SYSROOT "/sys/block"

static unsigned verbose;

static inline int
verbf(const char *fmt,...){
	va_list ap;
	int v;

	va_start(ap,fmt);
	if(verbose){
		v = vprintf(fmt,ap);
	}else{
		v = 0;
	}
	va_end(ap);
	return v;
}

// An (non-link) entry in the device hierarchy, representing a disk or
// partition.
typedef struct device {
	struct device *next;
	char name[PATH_MAX];	// Entry in /dev or /sys/block
	char path[PATH_MAX];	// Device topology, not filesystem
	char *id,*label;	// Not all have one
	// FIXME these aren't yet being set
	unsigned logical;	// Logical sector size
	unsigned physical;	// Physical sector size
} device;

static device *devs;
static int sysfd = -1; // Hold a reference to SYSROOT

static void
free_devtable(void){
	device *d;

	while( (d = devs) ){
		devs = d->next;
		free(d);
	}
}

static inline device *
create_new_device(const char *name){
	device *d;
	int fd;

	if(strlen(name) >= sizeof(d->name)){
		fprintf(stderr,"Name too long: %s\n",name);
		return NULL;
	}
	if((fd = openat(sysfd,name,O_CLOEXEC)) < 0){
		if(errno == ENOMEDIUM){
			// unloaded?
		}else{
			fprintf(stderr,"Couldn't open sys:%s (%s?)\n",name,strerror(errno));
			return NULL;
		}
	}else{
		struct sg_io_hdr sg;
		int r;

		memset(&sg,0,sizeof(sg));
		sg.interface_id = 'S'; // SCSI
		r = ioctl(fd,SG_IO,&sg,sizeof(sg));
		close(fd);
		if(r != 0){
			fprintf(stderr,"Couldn't run SG_IO on %s (%s?)\n",name,strerror(errno));
			return NULL;
		}
	}
	if( (d = malloc(sizeof(*d))) ){
		memset(d,0,sizeof(*d));
		d->id = d->label = NULL;
		strcpy(d->name,name);
	}else{
		fprintf(stderr,"Couldn't look up %s (%s?)\n",name,strerror(errno));
	}
	return d;
}

// Strips leading "../"s and "./"s, for better or worse.
/* static device *
lookup_device(const char *name){
	device *d;
	size_t s;

	do{
		if(strncmp(name,"/",1) == 0){
			s = 1;
		}else if(strncmp(name,"./",2) == 0){
			s = 2;
		}else if(strncmp(name,"../",3) == 0){
			s = 3;
		}else{
			s = 0;
		}
		name += s;
	}while(s);
	for(d = devs ; d ; d = d->next){
		if(strcmp(name,d->name) == 0){
			return d;
		}
	}
	if( (d = create_new_device(name)) ){
		d->next = devs;
		devs = d;
	}
	return d;
} */

static inline int
inotify_fd(void){
	int fd;

	if((fd = inotify_init1(IN_CLOEXEC)) < 0){
		fprintf(stderr,"Coudln't get inotify fd (%s?)\n",strerror(errno));
	}
	return fd;
}

static inline int
watch_dir(int fd,const char *dfp){
	//struct dirent *d;
	//DIR *dir;
	int wfd,r;
	//int dfd;

	wfd = inotify_add_watch(fd,dfp,IN_CREATE|IN_DELETE|IN_MOVED_FROM|IN_MOVED_TO);
	if(wfd < 0){
		fprintf(stderr,"Coudln't inotify on %s (%s?)\n",dfp,strerror(errno));
		return -1;
	}else{
		verbf("Watching %s on fd %d\n",dfp,wfd);
	}
	r = 0;
	/*
	if((dir = opendir(dfp)) == NULL){
		fprintf(stderr,"Coudln't open %s (%s?)\n",dfp,strerror(errno));
		inotify_rm_watch(fd,wfd);
		return -1;
	}
	if((dfd = dirfd(dir)) < 0){
		fprintf(stderr,"Coudln't get fd on %s (%s?)\n",dfp,strerror(errno));
		inotify_rm_watch(fd,wfd);
		closedir(dir);
		return -1;
	}
	while( errno = 0, (d = readdir(dir)) ){
		r = -1;
		if(d->d_type == DT_LNK){
			char buf[PATH_MAX] = "";

			if(readlinkat(dfd,d->d_name,buf,sizeof(buf)) < 0){
				fprintf(stderr,"Couldn't read link at %s/%s (%s?)\n",
					dfp,d->d_name,strerror(errno));
			}else{
				const device *dev;

				if((dev = lookup_device(buf)) == NULL){
					break;
				}
				verbf("%s -> %s\n",d->d_name,buf);
			}
		}
		r = 0;
	}
	if(r == 0 && errno){
		fprintf(stderr,"Error reading %s (%s?)\n",dfp,strerror(errno));
		r = -1;
	}
	closedir(dir);
	*/
	return r;
}

static void
usage(const char *name,int status){
	FILE *fp = status == EXIT_SUCCESS ? stdout : stderr;

	fprintf(fp,"usage: %s [ -h|--help ] [ -v|--verbose ]\n",name);
	exit(status);
}

static int
get_dir_fd(DIR **dir,const char *root){
	int fd;

	if((*dir = opendir(root)) == NULL){
		fprintf(stderr,"Couldn't open directory at %s (%s?)\n",root,strerror(errno));
		return -1;
	}
	if((fd = dirfd(*dir)) < 0){
		fprintf(stderr,"Couldn't get dirfd at %s (%s?)\n",root,strerror(errno));
		closedir(*dir);
		*dir = NULL;
	}
	return fd;
}

int main(int argc,char **argv){
	static const struct option ops[] = {
		{
			.name = "help",
			.has_arg = 0,
			.flag = NULL,
			.val = 'h',
		},{
			.name = "verbose",
			.has_arg = 0,
			.flag = NULL,
			.val = 'v',
		},{
			.name = NULL,
			.has_arg = 0,
			.flag = NULL,
			.val = 0,
		},
	};
	int fd,opt,longidx;
	DIR *sdir;

	opterr = 1;
	while((opt = getopt_long(argc,argv,"hv",ops,&longidx)) >= 0){
		switch(opt){
		case 'h':{
			usage(argv[0],EXIT_SUCCESS);
			break;
		}case 'v':{
			verbose = 1;
			break;
		}case ':':{
			fprintf(stderr,"Option requires argument: '%c'\n",optopt);
			usage(argv[0],EXIT_FAILURE);
			break;
		}case '?':{
			fprintf(stderr,"Unknown option: '%c'\n",optopt);
			usage(argv[0],EXIT_FAILURE);
			break;
		}default:{
			fprintf(stderr,"Unknown option: '%c'\n",optopt);
			usage(argv[0],EXIT_FAILURE);
			break;
		} }
	}
	printf("%s %s (libblkid %s)\n",PACKAGE,PACKAGE_VERSION,BLKID_VERSION);
	if((sysfd = get_dir_fd(&sdir,SYSROOT)) < 0){
		return EXIT_FAILURE;
	}
	if((fd = inotify_fd()) < 0){
		return EXIT_FAILURE;
	}
	if(load_blkid_superblocks()){
		fprintf(stderr,"Error in libblkid iteration (%s?)\n",strerror(errno));
		free_devtable();
		return EXIT_FAILURE;
	}
	if(watch_dir(fd,SYSROOT)){
		free_devtable();
		return EXIT_FAILURE;
	}
	free_devtable();
	return EXIT_SUCCESS;
}

#include <fcntl.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#include <mmap.h>
#include <mounts.h>

static int
parse_mount(const char *map,off_t len,char **dev,char **mnt,char **fs,char **ops){
	const char *t;
	int r = 0;

	*dev = *mnt = *fs = *ops = NULL;
	t = map;
	if(len <= t - map){
		goto err;
	}
	while(isgraph(map[r])){
		if(++r >= len){
			goto err;
		}
	}
	if(!isspace(map[r])){
		goto err;
	}
	if(r == t - map){
		goto err;
	}
	if((*dev = strndup(t,r - (t - map))) == NULL){
		goto err;
	}
	t = map + ++r;
	if(len <= t - map){
		goto err;
	}
	while(isgraph(map[r])){
		if(++r >= len){
			goto err;
		}
	}
	if(!isspace(map[r])){
		goto err;
	}
	if(r == t - map){
		goto err;
	}
	if((*mnt = strndup(t,r - (t - map))) == NULL){
		goto err;
	}
	t = map + ++r;
	if(len <= t - map){
		goto err;
	}
	while(isgraph(map[r])){
		if(++r >= len){
			goto err;
		}
	}
	if(!isspace(map[r])){
		goto err;
	}
	if(r == t - map){
		goto err;
	}
	if((*fs = strndup(t,r - (t - map))) == NULL){
		goto err;
	}
	t = map + ++r;
	if(len <= t - map){
		goto err;
	}
	while(isgraph(map[r])){
		if(++r >= len){
			goto err;
		}
	}
	if(!isspace(map[r])){
		goto err;
	}
	if(r == t - map){
		goto err;
	}
	if((*ops = strndup(t,r - (t - map))) == NULL){
		goto err;
	}
	t = map + ++r;
	while(r < len){
		if(map[r] == '\n'){
			break;
		}
		++r;
	}
	if(r >= len){
		goto err;
	}
	++r;
	return r;

err:
	free(*dev);
	free(*mnt);
	free(*fs);
	free(*ops);
	return -1;
}

int parse_mounts(const char *fn){
	char *mnt,*dev,*ops,*fs;
	off_t len,idx;
	char *map;
	int fd;

	if((map = map_virt_file(fn,&fd,&len)) == MAP_FAILED){
		return -1;
	}
	idx = 0;
	while(idx < len){
		int r;

		if((r = parse_mount(map + idx,len - idx,&dev,&mnt,&fs,&ops)) < 0){
			goto err;
		}
		free(mnt);
		free(dev);
		free(ops);
		free(fs);
		idx += r;
	}
	munmap_virt(map,len);
	close(fd);
	return 0;

err:
	munmap_virt(map,len);
	close(fd);
	return -1;
}
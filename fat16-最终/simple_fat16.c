#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <time.h>
#define FUSE_USE_VERSION 26
#include <fuse.h>
#include "fat16.h"
#define MINUTES 60
#define HOURS 3600
#define DAYS 86400
char *FAT_FILE_NAME = "fat16.img";
//该函数不是线程安全的：修改了调用的fd，只能加锁
void sector_read(unsigned int secnum, void *buffer)
{
    FILE *fd;
    fd = fopen(FAT_FILE_NAME, "rb");
    fseek(fd, BYTES_PER_SECTOR * secnum, SEEK_SET);
    fread(buffer, BYTES_PER_SECTOR, 1, fd);
    fclose(fd);
}
void sector_write(unsigned int secnum, void *buffer)
{
    FILE *fd;
    fd = fopen(FAT_FILE_NAME, "rb+");
    fseek(fd, BYTES_PER_SECTOR * secnum, SEEK_SET);
    fwrite(buffer, 1, BYTES_PER_SECTOR, fd);
    fclose(fd);
}
//将其分解但是不改变其字符串,作用，比对长文件名用
char **path_split(char *path, int *pathDepth_ret)
{
    int pathDepth = 1;
    char **paths = malloc(pathDepth * sizeof(char *));
    char *pathtail,*pathhead,*patht;
    char temp_char;
    char *temp;
    unsigned long int n = strlen(path)+1;
    pathtail = malloc(n*sizeof(char));
    pathhead = pathtail;
    patht = pathtail;//用于free
    strcpy(pathtail,path);
    while(*pathtail!='\0')
    {
        temp_char = *pathtail;
        if(temp_char=='/'&pathtail == pathhead)
        {
            pathtail++;
            pathhead++;
            continue;
        }//遇到了第一个/字符
        else if(temp_char == '/')//遇到了第一层
        {
            pathDepth++;
            paths = realloc(paths,pathDepth * sizeof(char *));
            *pathtail = '\0';
            pathtail++;
            temp = malloc(strlen(pathhead)*sizeof(char));
            strcpy(temp,pathhead);
            paths[pathDepth-2] = temp;
            pathhead = pathtail;
        }
        pathtail++;
    }
    n = strlen(pathhead)+1;
    temp = malloc(n*sizeof(char));
    strcpy(temp,pathhead);
    paths[pathDepth-1] = temp;
    *pathDepth_ret = pathDepth;
    free(patht);
    return paths;
}
//返回符合短文件名规格的字符串，flag为～N的N,短文件名只在创建的时候有用了，查找不看他
//path会被改变
//最多允许5个短文件重名
char *path_format(char *path,char flag)
{
    char *pathret = malloc(11*sizeof(char));
    int pathnum = 0;
    char temp_char;
    char *pathp = path;
    while(pathnum<6)
    {
        temp_char = *pathp;
        if(temp_char == '\0')//低于6个字节的目录文件
        {
            while(pathnum<11)
            {
                pathret[pathnum] = ' ';
                pathnum++;
            }
            return pathret;
        }
        if(temp_char>96&&temp_char<123)
        {
            pathret[pathnum] = *pathp -32;
        }
        else if(temp_char == '.')//文件名长度最长为5的普通文件
        {
            while(pathnum<8)
            {
                pathret[pathnum] = ' ';
                pathnum++;
            }
            pathp++;
            while(pathnum<11)
            {
                temp_char = *pathp;
                if(temp_char == '\0')//不足3个字节的扩展名
                {
                    while(pathnum<11)
                        pathret[pathnum] = ' ';
                    return pathret;
                }
                if(temp_char>96&&temp_char<123)
                    pathret[pathnum] = *pathp - 32;
                else
                    pathret[pathnum] = *pathp;
                pathp++;
                pathnum++;
            }
            return pathret;
        }
        else
            pathret[pathnum] = *pathp;
        pathnum++;
        pathp++;
    }
    //文件名起码大于或等于6个
    pathret[pathnum] = '~';
    pathret[pathnum+1] = flag + 0x30;
    pathnum+=2;
    while(*pathp!='\0')
    {
        if(*pathp == '.')//是文件
        {
            pathp++;
            while(pathnum<11)
            {
                temp_char = *pathp;
                if(temp_char == '\0')//不足3个字节的扩展名
                {
                    while(pathnum<11)
                        pathret[pathnum] = ' ';
                    return pathret;
                }
                if(temp_char>96&&temp_char<123)
                    pathret[pathnum] = *pathp - 32;
                else
                    pathret[pathnum] = *pathp;
                pathp++;
                pathnum++;
            }
            return pathret;
        }
        pathp++;
    }
    //跳出来，说明是目录
    while(pathnum<11)
    {
        pathret[pathnum] = ' ';
        pathnum++;
    }
    return pathret;
}
FAT16 *pre_init_fat16(void)
{
    FAT16 *fat16_ins;
    FILE *fd;
    fd = fopen(FAT_FILE_NAME, "rb");
    if (fd == NULL)
    {
        fprintf(stderr, "Missing FAT16 image file!\n");
        exit(EXIT_FAILURE);
    }
    fat16_ins = malloc(sizeof(FAT16));
    fread(&fat16_ins->Bpb,sizeof(BYTE), sizeof(BPB_BS), fd);
    fat16_ins->FirstRootDirSecNum =fat16_ins->Bpb.BPB_RsvdSecCnt + (fat16_ins->Bpb.BPB_NumFATS * fat16_ins->Bpb.BPB_FATSz16);
    fat16_ins->FirstDataSector = fat16_ins->Bpb.BPB_RsvdSecCnt + (fat16_ins->Bpb.BPB_NumFATS * fat16_ins->Bpb.BPB_FATSz16) + ((fat16_ins->Bpb.BPB_RootEntCnt * 32) + (fat16_ins->Bpb.BPB_BytsPerSec -1) )/ fat16_ins->Bpb.BPB_BytsPerSec;
    fclose(fd);
    return fat16_ins;
}
WORD fat_entry_by_cluster(FAT16 *fat16_ins, WORD ClusterN)
{
    BYTE sector_buffer[BYTES_PER_SECTOR];
    WORD ThisFATSecNum,ThisFATEntOffset,FATOffset;
    FATOffset = ClusterN *2;
    ThisFATSecNum = fat16_ins->Bpb.BPB_RsvdSecCnt + (FATOffset / fat16_ins->Bpb.BPB_BytsPerSec);
    ThisFATEntOffset = FATOffset % fat16_ins->Bpb.BPB_BytsPerSec;
    sector_read(ThisFATSecNum,sector_buffer);
    return *((WORD*)&sector_buffer[ThisFATEntOffset]);
}
void first_sector_by_cluster(FAT16 *fat16_ins, WORD ClusterN, WORD *FatClusEntryVal, WORD *FirstSectorofCluster, BYTE *buffer)
{
    *FatClusEntryVal = fat_entry_by_cluster(fat16_ins, ClusterN);
    *FirstSectorofCluster = ((ClusterN - 2) * fat16_ins->Bpb.BPB_SecPerClus) + fat16_ins->FirstDataSector;
    sector_read(*FirstSectorofCluster, buffer);
}
char *get_long_filename(int j, BYTE* buffer, BYTE* buffer_cache,char *shortname)
{
    int k,i,num,m;
    unsigned char flag,chksum;
    char *filename;
    char s[][3] = {".",".."};
    if(shortname[0] == '.')
    {
        if(shortname[1] == '.')
        {
            filename = malloc(3*sizeof(char));
            strcpy(filename,s[1]);
        }
        else
        {
            filename = malloc(2*sizeof(char));
            strcpy(filename,s[0]);
        }
        return filename;
    }
    for(k=11,m=0,chksum=0;k>0;k--)
        chksum = ((chksum&1)?0x80:0) + (chksum>>1) + shortname[m++];
    k = j;
    //机制：先确认0xD处的值，确认完后就拷贝，拷贝完之后判断0x0处是不是带掩码的，不是就继续往前找
    //长短文件名目录不一定是连续的
    //这一点没有考虑
    if(k == 0)//短文件名目录是该扇区的开头目录了,得倒回去找
    {
        memcpy(&flag,buffer_cache+BYTES_PER_SECTOR - 32 + 13,1);
        if(flag == chksum)//是配对的长文件名
        {
            //先拷贝一份
            filename = malloc(13*sizeof(char));
            for(i=0,num=0;i<5;i++)
                filename[num++] = buffer_cache[BYTES_PER_SECTOR-32+1+2*i];
            for(i=0;i<6;i++)
                filename[num++] = buffer_cache[BYTES_PER_SECTOR-32+14+2*i];
            for(i=0;i<2;i++)
                filename[num++] = buffer_cache[BYTES_PER_SECTOR-32+28+2*i];
            memcpy(&flag,buffer_cache+BYTES_PER_SECTOR - 32,1);
            //判断其是否是掩码
            k = 2;
            while((flag|0x40) != flag)
            {
                memcpy(&flag,buffer_cache+BYTES_PER_SECTOR - 32*k + 13,1);
                //判断是否配对
                if(flag == chksum)//是配对的长文件名
                {
                    filename = realloc(filename,13*k*sizeof(char));
                    for(i=0;i<5;i++)
                        filename[num++] = buffer_cache[BYTES_PER_SECTOR-32*k+1+2*i];
                    for(i=0;i<6;i++)
                        filename[num++] = buffer_cache[BYTES_PER_SECTOR-32*k+14+2*i];
                    for(i=0;i<2;i++)
                        filename[num++] = buffer_cache[BYTES_PER_SECTOR-32*k+28+2*i];
                }
                else
                    break;//跳出,已经读取了所有的长文件名
                memcpy(&flag,buffer_cache+BYTES_PER_SECTOR - 32*k,1);
                k++;
            }
        }
    }
    else//短文件名不是扇区的开头
    {
        memcpy(&flag,buffer +32*(k-1) + 13,1);
        if (flag == chksum)
        {
            filename = malloc(13*sizeof(char));
            for(i=0,num=0;i<5;i++)
                filename[num++] = buffer[32*(k-1)+1+2*i];
            for(i=0;i<6;i++)
                filename[num++] = buffer[32*(k-1)+14+2*i];
            for(i=0;i<2;i++)
                filename[num++] = buffer[32*(k-1)+28+2*i];
            memcpy(&flag,buffer + 32*(k-1),1);
            k--;
            m=2;
            //得先从这个扇区开始找长文件名，如果找到了这个扇区的头部再找前一个
            while(k!=0)
            {
                if((flag|0x40) != flag)
                {
                    memcpy(&flag,buffer + 32*(k-1) + 13,1);
                    if(flag == chksum)
                    {
                        filename = realloc(filename,13*m*sizeof(char));
                        m++;
                        for(i=0;i<5;i++)
                            filename[num++] = buffer[32*(k-1)+1+2*i];
                        for(i=0;i<6;i++)
                            filename[num++] = buffer[32*(k-1)+14+2*i];
                        for(i=0;i<2;i++)
                            filename[num++] = buffer[32*(k-1)+28+2*i];
                    }
                    else//如果不配对跳出
                        break;
                }
                else
                    break;
                if(flag != chksum)//二次跳出
                    break;
                memcpy(&flag,buffer +32*(k-1),1);
                k--;
            }
            if(k == 0)//因为这个状况而跳出的话说明可能还没找完
            {
                while((flag|0x40) != flag)//还没找到全部的长文件目录项，继续往前
                {
                    k++;
                    memcpy(&flag,buffer_cache+BYTES_PER_SECTOR - 32*k + 13,1);
                    //判断是否配对
                    if(flag == chksum)//是配对的长文件名
                    {
                        filename = realloc(filename,13*m*sizeof(char));
                        m++;
                        for(i=0;i<5;i++)
                            filename[num++] = buffer_cache[BYTES_PER_SECTOR-32*k+1+2*i];
                        for(i=0;i<6;i++)
                            filename[num++] = buffer_cache[BYTES_PER_SECTOR-32*k+14+2*i];
                        for(i=0;i<2;i++)
                            filename[num++] = buffer_cache[BYTES_PER_SECTOR-32*k+28+2*i];
                    }
                    else
                        break;//跳出,已经读取了所有的长文件名
                    memcpy(&flag,buffer_cache+BYTES_PER_SECTOR - 32*k,1);
                }
            }
        }
    }
    return filename;
}
int find_root(FAT16 *fat16_ins, DIR_ENTRY *Dir, const char *path,WORD* SecNum,int* num)//额外返回第几个扇区的第几个目录项，目的是为了给write更新用
{
    int pathDepth;
    char **paths = path_split((char *)path, &pathDepth);
    int i, j;
    int RootDirCnt = 1;
    char *name;
    char shortname[11];
    BYTE buffer[BYTES_PER_SECTOR],buffer_cache[BYTES_PER_SECTOR];
    sector_read(fat16_ins->FirstRootDirSecNum, buffer);
    for (i = 1; i <= fat16_ins->Bpb.BPB_RootEntCnt; i++)
    {
        j=(i-1)%(BYTES_PER_SECTOR/32);
        if(j==0&&i>1)//读完了一个扇区
        {
            memcpy(buffer_cache,buffer,BYTES_PER_SECTOR);//缓存一个扇区,因为可能会存有长文件目录
            sector_read(fat16_ins->FirstRootDirSecNum+RootDirCnt, buffer);
            RootDirCnt++;
        }
        //根目录不可能出现第一个就是短文件目录项的情况
        if(buffer[32*j] == 0x00)//后面没有目录了！
            return 1;
        if(buffer[32*j+11] == 0xf||buffer[32*j] == 0xe5)
            continue;
        //能到这边说明是一个短目录项
        memcpy(shortname,buffer+32*j,11);
        name = get_long_filename(j,buffer,buffer_cache,shortname);
        if(strcmp(name,paths[0])==0)
        {
            free(name);
            memcpy(Dir,buffer+32*j,32);
            if(pathDepth>1)
                return find_subdir(fat16_ins,Dir,paths,pathDepth,1,SecNum,num);
            else
            {
                for(i = 0;i<pathDepth;i++)
                {
                    free(paths[i]);
                }
                free(paths);
                *SecNum = fat16_ins->FirstRootDirSecNum+RootDirCnt-1;
                *num = j;
                return 0;
            }
        }
        free(name);
    }
    //没找到，free掉
    for(i = 0;i<pathDepth;i++)
    {
        free(paths[i]);
    }
    free(paths);
    return 1;
}
int find_subdir(FAT16 *fat16_ins, DIR_ENTRY *Dir, char **paths, int pathDepth, int curDepth,WORD* SecNum,int*num)
{
    int i, j;
    int DirSecCnt = 1;  /* 用于统计已读取的扇区数 */
    BYTE buffer[BYTES_PER_SECTOR],buffer_cache[BYTES_PER_SECTOR];
    WORD ClusterN, FatClusEntryVal, FirstSectorofCluster;
    char shortname[11];
    char* name;
    ClusterN = Dir->DIR_FstClusLO;
    first_sector_by_cluster(fat16_ins, ClusterN, &FatClusEntryVal, &FirstSectorofCluster, buffer);
    i=1;
    j=0;
    while(buffer[32*j]!=0x00)
    {
        if(buffer[32*j+11]!=0x0f)
        {
            memcpy(shortname,buffer+32*j,11);
            name = get_long_filename(j,buffer,buffer_cache,shortname);
            if(strcmp(name,paths[curDepth])==0)
            {
                memcpy(Dir->DIR_Name,buffer+32*j,32);
                if(pathDepth>curDepth+1)
                    return find_subdir(fat16_ins,Dir,paths,pathDepth,curDepth+1,SecNum,num);
                else
                {
                    //free掉
                    for(i = 0;i<pathDepth;i++)
                    {
                        free(paths[i]);
                    }
                    free(paths);
                    *SecNum = FirstSectorofCluster+DirSecCnt-1;
                    *num = j;
                    return 0;
                }
            }
        }
        i++;
        j=(i-1)%(BYTES_PER_SECTOR/32);
        if(j==0&&i>1)//读完了一个扇区
        {
            memcpy(buffer_cache,buffer,BYTES_PER_SECTOR);//缓存一个扇区,因为可能会存有长文件目录
            if(DirSecCnt == fat16_ins->Bpb.BPB_SecPerClus)//读完了整个簇
            {
                ClusterN = FatClusEntryVal;
                if(ClusterN == 0xffff)//没了，不用读了
                    return 1;
                first_sector_by_cluster(fat16_ins,ClusterN,&FatClusEntryVal,&FirstSectorofCluster,buffer);
                DirSecCnt = 1;
            }
            else
            {
                sector_read(FirstSectorofCluster+DirSecCnt, buffer);
                DirSecCnt++;
            }
        }
    }
    //没找到，也要free掉
    for(i = 0;i<pathDepth;i++)
    {
        free(paths[i]);
    }
    free(paths);
    return 1;
}
void *fat16_init(struct fuse_conn_info *conn)
{
    struct fuse_context *context;
    context = fuse_get_context();
    
    return context->private_data;
}

void fat16_destroy(void *data)
{
    free(data);
}

int fat16_getattr(const char *path, struct stat *stbuf)
{
    FAT16 *fat16_ins;
    
    struct fuse_context *context;
    context = fuse_get_context();
    //需要改变，这里全都是返回指针
    fat16_ins = (FAT16 *)context->private_data;
    
    memset(stbuf, 0, sizeof(struct stat));
    stbuf->st_dev = fat16_ins->Bpb.BS_VollID;
    stbuf->st_blksize = BYTES_PER_SECTOR * fat16_ins->Bpb.BPB_SecPerClus;
    stbuf->st_uid = getuid();
    stbuf->st_gid = getgid();
    
    if (strcmp(path, "/") == 0)
    {
        stbuf->st_mode = S_IFDIR | S_IRWXU;
        stbuf->st_size = 0;
        stbuf->st_blocks = 0;
        stbuf->st_ctime = stbuf->st_atime = stbuf->st_mtime = 0;
    }
    else
    {
        DIR_ENTRY Dir;
        int j;
        WORD i;
        int res = find_root(fat16_ins, &Dir, path,&i,&j);
        
        if (res == 0)
        {
            if (Dir.DIR_Attr == ATTR_DIRECTORY)
            {
                stbuf->st_mode = S_IFDIR | 0755;
            }
            else
            {
                stbuf->st_mode = S_IFREG | 0755;
            }
            stbuf->st_size = Dir.DIR_FileSize;
            
            if (stbuf->st_size % stbuf->st_blksize != 0)
            {
                stbuf->st_blocks = (int)(stbuf->st_size / stbuf->st_blksize) + 1;
            }
            else
            {
                stbuf->st_blocks = (int)(stbuf->st_size / stbuf->st_blksize);
            }
            
            struct tm t;
            memset((char *)&t, 0, sizeof(struct tm));
            t.tm_sec = Dir.DIR_WrtTime & ((1 << 5) - 1);
            t.tm_min = (Dir.DIR_WrtTime >> 5) & ((1 << 6) - 1);
            t.tm_hour = Dir.DIR_WrtTime >> 11;
            t.tm_mday = (Dir.DIR_WrtDate & ((1 << 5) - 1));
            t.tm_mon = (Dir.DIR_WrtDate >> 5) & ((1 << 4) - 1);
            t.tm_year = 80 + (Dir.DIR_WrtDate >> 9);
            stbuf->st_ctime = stbuf->st_atime = stbuf->st_mtime = mktime(&t);
        }
        else
            return -ENOENT;
    }
    return 0;
}

int fat16_readdir(const char *path, void *buffer, fuse_fill_dir_t filler,
                  off_t offset, struct fuse_file_info *fi)
{
    FAT16 *fat16_ins;
    BYTE sector_buffer[BYTES_PER_SECTOR],sector_buffer_temp[BYTES_PER_SECTOR];
    int i,j,k,m,chknum;
    int RootDirCnt = 1, DirSecCnt = 1;  /* 用于统计已读取的扇区数 */
    struct fuse_context *context;
    context = fuse_get_context();
    fat16_ins = (FAT16 *)context->private_data;
    char * filename;
    char flag,temp;
    if (strcmp(path, "/") == 0)
    {
        DIR_ENTRY Root;
        sector_read(fat16_ins->FirstRootDirSecNum, sector_buffer);
        for (i = 1; i <= fat16_ins->Bpb.BPB_RootEntCnt; i++)
        {
            j=(i-1)%(BYTES_PER_SECTOR/32);
            if(j==0&&i>1)//读完了一个扇区,为了长文件名，缓存这个扇区
            {
                memcpy(sector_buffer_temp,sector_buffer,BYTES_PER_SECTOR);
                sector_read(fat16_ins->FirstRootDirSecNum+RootDirCnt, sector_buffer);
                RootDirCnt++;
            }
            if(sector_buffer[32*j] == 0x00)//发现相同
                break;
            else if(sector_buffer[32*j] == 0xE5)
                continue;
            else
            {
                if(sector_buffer[32*j+11]!=0x0f)
                {
                    memcpy(Root.DIR_Name,sector_buffer+32*j,11);
                    filename = get_long_filename(j,sector_buffer,sector_buffer_temp,Root.DIR_Name);
                    filler(buffer,filename,NULL,0);
                    free(filename);
                }
            }
        }
    }
    else
    {
        DIR_ENTRY Dir;
        WORD ClusterN, FatClusEntryVal, FirstSectorofCluster;
        find_root(fat16_ins, &Dir, path,&ClusterN,&j);
        ClusterN = Dir.DIR_FstClusLO;
        first_sector_by_cluster(fat16_ins, ClusterN, &FatClusEntryVal, &FirstSectorofCluster, sector_buffer);
        i = 1;
        j = 0;
        while(sector_buffer[32*j]!=0x00)
        {
            if(sector_buffer[32*j]!=0xE5)
            {
                memcpy(Dir.DIR_Name,sector_buffer+32*j,11);
                memcpy(&Dir.DIR_Attr,sector_buffer+32*j+11,1);
                if(Dir.DIR_Attr == 0x0f)//不合规格，跳过
                    ;
                else
                {
                    filename = get_long_filename(j,sector_buffer,sector_buffer_temp,Dir.DIR_Name);
                    filler(buffer,filename,NULL,0);
                    free(filename);
                }
            }
            i++;
            j=(i-1)%(BYTES_PER_SECTOR/32);
            if(j==0&&i>1)//读完了一个扇区
            {
                memcpy(sector_buffer_temp,sector_buffer,BYTES_PER_SECTOR);
                if(DirSecCnt == fat16_ins->Bpb.BPB_SecPerClus)//读完了整个簇
                {
                    ClusterN = FatClusEntryVal;
                    if(ClusterN == 0xffff)//没了，不用读了
                        return 0; first_sector_by_cluster(fat16_ins,ClusterN,&FatClusEntryVal,&FirstSectorofCluster,sector_buffer);
                    DirSecCnt = 1;
                }
                else
                {
                    sector_read(FirstSectorofCluster+DirSecCnt, sector_buffer);
                    DirSecCnt++;
                }
            }
        }
    }
    return 0;
}
int fat16_read(const char *path, char *buffer, size_t size, off_t offset,
               struct fuse_file_info *fi)
{
    FAT16 *fat16_ins;
    struct fuse_context *context;
    context = fuse_get_context();
    fat16_ins = (FAT16 *)context->private_data;
    DIR_ENTRY Dir;
    int ClusterNum,SecOffset,SecNum;
    WORD ClusterN, FatClusEntryVal, FirstSectorofCluster;
    BYTE sector_buffer[BYTES_PER_SECTOR];
    int temp,total;
    size_t real_size;
    if(find_root(fat16_ins, &Dir, path,&ClusterN,&total)==0)
    {
        if(offset>Dir.DIR_FileSize)
        {
            return 0;
        }
        ClusterN = Dir.DIR_FstClusLO;
        ClusterNum = (offset / fat16_ins->Bpb.BPB_BytsPerSec)/fat16_ins->Bpb.BPB_SecPerClus;
        SecNum = (offset / fat16_ins->Bpb.BPB_BytsPerSec)%fat16_ins->Bpb.BPB_SecPerClus;
        SecOffset = offset % fat16_ins->Bpb.BPB_BytsPerSec;
        temp = 0;
        FatClusEntryVal =  fat_entry_by_cluster(fat16_ins, ClusterN);
        while(temp<ClusterNum)
        {
            ClusterN = FatClusEntryVal;
            FatClusEntryVal =  fat_entry_by_cluster(fat16_ins, ClusterN);
            temp++;
        }
        FirstSectorofCluster = ((ClusterN - 2) * fat16_ins->Bpb.BPB_SecPerClus) + fat16_ins->FirstDataSector ;
        sector_read(FirstSectorofCluster+SecNum, sector_buffer);
        if(Dir.DIR_FileSize>=offset+size)
        {
            real_size = size;
        }
        else
        {
            real_size = Dir.DIR_FileSize - offset;
        }
        temp = BYTES_PER_SECTOR - SecOffset;
        if(temp > real_size)
        {
            memcpy(buffer,sector_buffer+SecOffset,real_size);
        }
        else
        {
            memcpy(buffer,sector_buffer+SecOffset,temp);
            total = temp;
            SecNum++;
            while(total < real_size)
            {
                if(SecNum == fat16_ins->Bpb.BPB_SecPerClus)//整个簇都读完了
                {
                    ClusterN = FatClusEntryVal;
                    first_sector_by_cluster(fat16_ins,ClusterN,&FatClusEntryVal,&FirstSectorofCluster,sector_buffer);
                    SecNum = 1;
                }
                else
                {
                    sector_read(FirstSectorofCluster+SecNum, sector_buffer);
                    SecNum++;
                }
                temp = (real_size - total > BYTES_PER_SECTOR)?BYTES_PER_SECTOR:(real_size - total);
                memcpy(buffer+total,sector_buffer,temp);
                total +=temp;
            }
        }
        return real_size;
    }
    return 0;
}
void format_date_time(WORD *date_format,WORD *time_format)
{
    time_t t;
    time(&t);
    int days_per_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int days_per_month_leap[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int year = 1970;
    int month = 0;
    int day = 0;
    int hours = 0;
    int min = 0;
    int sec = 0;
    int secs_year = DAYS * 365;
    int *dpm = days_per_month;
    
    while (t) {
        if (t >= secs_year) {
            year++;
            t -= secs_year;
            
            if (!(year % 400 && (year % 100 == 0 || (year & 3)))) {
                secs_year = DAYS * 366;
                dpm = days_per_month_leap;
            } else {
                secs_year = DAYS * 365;
                dpm = days_per_month;
            }
        } else {
            if (t >= dpm[month] * DAYS) {
                t -= dpm[month] * DAYS;
                month++;
            } else {
                day = t / DAYS;
                t -= day * DAYS;
                hours = t / HOURS;
                t -= hours * HOURS;
                min = t / MINUTES;
                t -= min * MINUTES;
                sec = t;
                t = 0;
            }
        }
    }
    *date_format =((year - 1980)<<9)|((month+1)<<5)|(day+1);
    if (time_format) {
        *time_format = ((hours)<<11)|((min)<<5)|(sec/2);
    }
}
//返回0表示没有这样的簇号
WORD find_empty_fat()
{
    FAT16 *fat16_ins;
    struct fuse_context *context;
    context = fuse_get_context();
    fat16_ins = (FAT16 *)context->private_data;
    BYTE buffer[BYTES_PER_SECTOR];
    int m,j,i=0;
    WORD ClusterN,FirstSectorofCluster;
    sector_read(fat16_ins->Bpb.BPB_RsvdSecCnt,buffer );
    while(i<fat16_ins->Bpb.BPB_FATSz16)
    {
        for(j=0;j<fat16_ins->Bpb.BPB_BytsPerSec/sizeof(WORD);j++)
        {
            if(buffer[2*j]==0x00&&buffer[2*j+1]==0x00)//找到未使用的
            {
                buffer[2*j] =0xff;
                buffer[2*j+1] = 0xff;
                sector_write(fat16_ins->Bpb.BPB_RsvdSecCnt+i, buffer);
		ClusterN = (j+(i*fat16_ins->Bpb.BPB_BytsPerSec/sizeof(WORD)));
		FirstSectorofCluster = ((ClusterN - 2) * fat16_ins->Bpb.BPB_SecPerClus) + fat16_ins->FirstDataSector;
		for(m=0;m<fat16_ins->Bpb.BPB_SecPerClus;m++)
		{
		    sector_read(FirstSectorofCluster+m, buffer);
		    memset(buffer,0,BYTES_PER_SECTOR);//初始化
		    sector_write(FirstSectorofCluster+m, buffer);
		}
                return ClusterN;
            }
        }
        i++;
        if(i<fat16_ins->Bpb.BPB_FATSz16)
            sector_read(fat16_ins->Bpb.BPB_RsvdSecCnt+i,buffer );
    }
    return 0;
}
void change_fat(FAT16* fat16_ins, WORD ClusterN, WORD Cluster)
{
    BYTE sector_buffer[BYTES_PER_SECTOR];
    WORD ThisFATSecNum,ThisFATEntOffset,FATOffset;
    FATOffset = ClusterN *2;
    ThisFATSecNum = fat16_ins->Bpb.BPB_RsvdSecCnt + (FATOffset / fat16_ins->Bpb.BPB_BytsPerSec);
    ThisFATEntOffset = FATOffset % fat16_ins->Bpb.BPB_BytsPerSec;
    sector_read(ThisFATSecNum,sector_buffer);
    *((WORD*)&sector_buffer[ThisFATEntOffset]) = Cluster;
    sector_write(ThisFATSecNum,sector_buffer);
}
void Create_shortname_Dir(char *shortname, BYTE mode,DIR_ENTRY* Dir)
{
    char s[][12] = {".          ","..         "};
    strncpy(Dir->DIR_Name, shortname, 11);
    Dir->DIR_Attr = mode;
    Dir->DIR_NTRes = 0;
    Dir->DIR_CrtTimeTenth = 0;
    format_date_time(&Dir->DIR_CrtDate, &Dir->DIR_CrtTime);
    Dir->DIR_LstAccDate = Dir->DIR_CrtDate;
    Dir->DIR_FstClusHI = 0;
    Dir->DIR_WrtTime = Dir->DIR_CrtTime;
    Dir->DIR_WrtDate = Dir->DIR_CrtDate;
    if(strncmp(shortname,s[0],11)&&strncmp(shortname,s[1],11))
        Dir->DIR_FstClusLO = find_empty_fat();
    Dir->DIR_FileSize = 0;//不管是文件还是目录都是先设为0
}
void Create_longname_Dir(char *path,char *shortname,LDIR_ENTRY* LDir,int* LDirCnt)
{
    unsigned long i = (strlen(path)+1);
    int k,num,m,h;
    unsigned char flag,chksum;
    for(k=11,m=0,chksum=0;k>0;k--)
        chksum = ((chksum&1)?0x80:0) + (chksum>>1) + shortname[m++];
    num = (i + (13-1))/13;
    h=0;
    *LDirCnt = num;
    for(k=0;k<num;k++)
    {
        if(k==num-1)
            LDir[k].LDIR_Ord = k+1|0x40;
        else
            LDir[k].LDIR_Ord = k+1;
        for(m=0;m<5;m++)
        {
            if(h<i)//还没全部填完
            {
                LDir[k].LDIR_Name1[2*m] = path[h++];
                LDir[k].LDIR_Name1[2*m+1] = 0;
            }
            else
            {
                LDir[k].LDIR_Name1[2*m+1] = 0xff;
                LDir[k].LDIR_Name1[2*m+2] = 0xff;
            }
        }
        LDir[k].LDIR_Attr = ATTR_LONG_NAME;
        LDir[k].LDIR_Type = 0;
        LDir[k].LDIR_Chksum = chksum;
        for(m=0;m<6;m++)
        {
            if(h<i)//还没全部填完
            {
                LDir[k].LDIR_Name2[2*m] = path[h++];
                LDir[k].LDIR_Name2[2*m+1] = 0;
            }
            else
            {
                LDir[k].LDIR_Name2[2*m] = 0xff;
                LDir[k].LDIR_Name2[2*m+1] = 0xff;
            }
        }
        LDir[k].LDIR_FstClusLO = 0x0000;
        for(m=0;m<2;m++)
        {
            if(h<i)//还没全部填完
            {
                LDir[k].LDIR_Name3[2*m] = path[h++];
                LDir[k].LDIR_Name3[2*m+1] = 0;
            }
            else
            {
                LDir[k].LDIR_Name3[2*m] = 0xff;
                LDir[k].LDIR_Name3[2*m+1] = 0xff;
            }
        }
    }
}
//成功则return 1 失败0
int fat16_mkdir(const char *path, mode_t mode)
{
    FAT16 *fat16_ins;
    BYTE sector_buffer[BYTES_PER_SECTOR],sector_buffer_temp[BYTES_PER_SECTOR];
    int pathDepth;
    int i, j,flag,len,LongDirCnt;
    int RootDirCnt = 1;
    int DirSecCnt = 1;
    char **paths;
    char *pathc;
    char *temp_path;
    char s[][12] = {".          ","..         "};
    struct fuse_context *context;
    WORD ClusterN, FatClusEntryVal, FirstSectorofCluster,SecNum,Parent_ClusterN;
    context = fuse_get_context();
    LDIR_ENTRY LDir[10];
    DIR_ENTRY Dir;
    fat16_ins = (FAT16 *)context->private_data;
    paths = path_split((char *)path, &pathDepth);
    if (pathDepth==1)
    {
        //在根目录创建
        flag = 1;
        pathc = path_format(paths[0], 1);//1是暂时设置的，目的只是为了生成一个可以用来比对的字符串
        sector_read(fat16_ins->FirstRootDirSecNum, sector_buffer);
        for (i = 1; i <= fat16_ins->Bpb.BPB_RootEntCnt; i++)
        {
            j=(i-1)%(BYTES_PER_SECTOR/32);
            if(j==0&&i>1)//读完了一个扇区
            {
                memcpy(sector_buffer_temp,sector_buffer,BYTES_PER_SECTOR);
                sector_read(fat16_ins->FirstRootDirSecNum+RootDirCnt, sector_buffer);
                RootDirCnt++;
            }
            if(sector_buffer[32*j]==0x00)//发现了一个空的目录项，插进去
            {
                free(pathc);
                pathc = path_format(paths[0], flag);
                Create_shortname_Dir(pathc,ATTR_DIRECTORY , &Dir);
                Create_longname_Dir(paths[0], pathc, LDir, &LongDirCnt);
                //todo:进入目录内设置.和..两个目录
                //这里没有考虑根目录项数不够的情况
                //查看是否跨扇区
                if((j+LongDirCnt)<BYTES_PER_SECTOR/32)
                {
                    memcpy(sector_buffer+32*(j+LongDirCnt),&Dir,32);
                    LongDirCnt--;
                    flag=0;
                }
                else
                {
                    sector_read(fat16_ins->FirstRootDirSecNum+RootDirCnt,sector_buffer_temp);//取下一个扇区
                    i = (j+LongDirCnt)%BYTES_PER_SECTOR/32;
                    LongDirCnt = LongDirCnt-i-1;
                    memcpy(sector_buffer_temp+32*i,&Dir,32);
                    i--;
                    flag=0;
                    while(i>=0)
                    {
                        memcpy(sector_buffer_temp+32*i,&LDir[flag],32);
                        flag++;
                        i--;
                    }
                    sector_write(fat16_ins->FirstRootDirSecNum+RootDirCnt, sector_buffer_temp);
                }
                while(LongDirCnt>=0)
                {
                    memcpy(sector_buffer+32*(j+LongDirCnt),&LDir[flag],32);
                    LongDirCnt--;
                    flag++;
                }
                sector_write(fat16_ins->FirstRootDirSecNum+RootDirCnt-1, sector_buffer);
                first_sector_by_cluster(fat16_ins, Dir.DIR_FstClusLO, &FatClusEntryVal, &FirstSectorofCluster, sector_buffer);
                //设置. ..
                Create_shortname_Dir(s[0],ATTR_DIRECTORY , &Dir);
                memcpy(sector_buffer,&Dir,32);
                Create_shortname_Dir(s[1],ATTR_DIRECTORY , &Dir);
                Dir.DIR_FstClusLO = 0x0000;
                memcpy(sector_buffer+32,&Dir,32);
                sector_write(FirstSectorofCluster, sector_buffer);
                //free掉
                free(pathc);
                for(i = 0;i<pathDepth;i++)
                {
                    free(paths[i]);
                }
                free(paths);
                return 0;
            }
            //先找有没有空的位置，没有再覆盖写过但是删除的（todo）
            else if(sector_buffer[32*j+11]!=0x0f&&strncmp(sector_buffer+32*j, pathc,6)==0)//不是长文件名目录项且名字相同
            {
                flag++;
            }
        }
    }
    else
    {
        temp_path = malloc((strlen(path)+1));
        strcpy(temp_path,path);
        pathc = temp_path;
        i=pathDepth;
        while(i>1)
        {
            pathc++;
            if(*pathc=='/')//因为path_split会改变path，所以需要还原
            {
                i--;
            }
        }
        *pathc = '\0';
        if(find_root(fat16_ins, &Dir, temp_path,&ClusterN,&j)==0)
        {
            flag = 1;
            pathc = path_format(paths[pathDepth-1], 1);//1是暂时设置的，目的只是为了生成一个可以用来比对的字符串
            ClusterN = Dir.DIR_FstClusLO;
            Parent_ClusterN = Dir.DIR_FstClusLO;
            first_sector_by_cluster(fat16_ins, ClusterN, &FatClusEntryVal, &FirstSectorofCluster, sector_buffer);
            i = 1;
            j = 0;
            while(sector_buffer[32*j]!=0x00)
            {
                if(sector_buffer[32*j+11]!=0x0f&&strncmp(sector_buffer+32*j, pathc,6)==0)//不是长文件名目录项且名字相同
                {
                    flag++;
                }
                i++;
                j=(i-1)%(BYTES_PER_SECTOR/32);
                if(j==0&&i>1)//读完了一个扇区
                {
                    memcpy(sector_buffer_temp,sector_buffer,BYTES_PER_SECTOR);
                    if(DirSecCnt == fat16_ins->Bpb.BPB_SecPerClus)//读完了整个簇
                    {
                        SecNum = FatClusEntryVal;
                        if(SecNum == 0xffff)//空的
                        {
                            SecNum = find_empty_fat();//找个新的簇
                            change_fat(fat16_ins,ClusterN,SecNum);
                        }
                        ClusterN = SecNum; first_sector_by_cluster(fat16_ins,ClusterN,&FatClusEntryVal,&FirstSectorofCluster,sector_buffer);
                        DirSecCnt = 1;
                    }
                    else
                    {
                        sector_read(FirstSectorofCluster+DirSecCnt, sector_buffer);
                        DirSecCnt++;
                    }
                }
            }
            free(pathc);
            pathc = path_format(paths[pathDepth-1], flag);
            Create_shortname_Dir(pathc,ATTR_DIRECTORY , &Dir);
            Create_longname_Dir(paths[pathDepth-1], pathc, LDir, &LongDirCnt);
            //这里没有考虑根目录项数不够的情况
            //未考虑边界条件，就是总目录项数如果多于当前扇区空余目录项数
            if((j+LongDirCnt)<BYTES_PER_SECTOR/32)
            {
                memcpy(sector_buffer+32*(j+LongDirCnt),&Dir,32);
                LongDirCnt--;
                flag=0;
            }
            else
            {
                if(DirSecCnt<fat16_ins->Bpb.BPB_SecPerClus)
                {
                    sector_read(FirstSectorofCluster+DirSecCnt,sector_buffer_temp);//取下一个扇区
                    SecNum = FirstSectorofCluster+DirSecCnt;
                }
                else
                {
                    if(FatClusEntryVal == 0xffff)
                    {
                        FatClusEntryVal = find_empty_fat();
                        change_fat(fat16_ins,ClusterN,FatClusEntryVal);
                    }
                    ClusterN = FatClusEntryVal;
                    first_sector_by_cluster(fat16_ins,ClusterN,&FatClusEntryVal,&SecNum,sector_buffer_temp);
                }
                i = (j+LongDirCnt)%BYTES_PER_SECTOR/32;
                LongDirCnt = LongDirCnt-i-1;
                memcpy(sector_buffer_temp+32*i,&Dir,32);
                i--;
                flag=0;
                while(i>=0)
                {
                    memcpy(sector_buffer_temp+32*i,&LDir[flag],32);
                    flag++;
                    i--;
                }
                sector_write(SecNum,sector_buffer_temp);
            }
            while(LongDirCnt>=0)
            {
                memcpy(sector_buffer+32*(j+LongDirCnt),&LDir[flag],32);
                LongDirCnt--;
                flag++;
            }
            sector_write(FirstSectorofCluster+DirSecCnt-1, sector_buffer);
            first_sector_by_cluster(fat16_ins, Dir.DIR_FstClusLO, &FatClusEntryVal, &FirstSectorofCluster, sector_buffer);
            //设置. ..
            Create_shortname_Dir(s[0],ATTR_DIRECTORY , &Dir);
            memcpy(sector_buffer,&Dir,32);
            Create_shortname_Dir(s[1],ATTR_DIRECTORY , &Dir);
            Dir.DIR_FstClusLO = Parent_ClusterN;
            memcpy(sector_buffer+32,&Dir,32);
            sector_write(FirstSectorofCluster, sector_buffer);
            //free掉
            free(pathc);
            for(i = 0;i<pathDepth;i++)
            {
                free(paths[i]);
            }
            free(temp_path);
            free(paths);
            return 0;
        }
        free(temp_path);
    }
    for(i = 0;i<pathDepth;i++)
    {
        free(paths[i]);
    }
    free(paths);
    return 0;
}
//返回写入字节数
int fat16_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi)
{
    FAT16 *fat16_ins;
    struct fuse_context *context;
    context = fuse_get_context();
    //返回指针，改变
    fat16_ins = (FAT16 *)context->private_data;
    DIR_ENTRY Dir;
    int DirNum;
    WORD DirSecNum;
    int ClusterNum,SecOffset,SecNum;
    WORD ClusterN, FatClusEntryVal, FirstSectorofCluster;
    BYTE sector_buffer[BYTES_PER_SECTOR];
    int temp,total;
    if(find_root(fat16_ins, &Dir, path,&DirSecNum,&DirNum)==0)
    {
        ClusterN = Dir.DIR_FstClusLO;
        ClusterNum = (offset / fat16_ins->Bpb.BPB_BytsPerSec)/fat16_ins->Bpb.BPB_SecPerClus;
        SecNum = (offset / fat16_ins->Bpb.BPB_BytsPerSec)%fat16_ins->Bpb.BPB_SecPerClus;
        SecOffset = offset % fat16_ins->Bpb.BPB_BytsPerSec;
        temp = 0;
        FatClusEntryVal =  fat_entry_by_cluster(fat16_ins, ClusterN);
        while(temp<ClusterNum)
        {
            if(FatClusEntryVal == 0xffff)//需要再跳一个簇然而已经没有分配新的簇了
            {
                FatClusEntryVal = find_empty_fat();//找个新的簇
                change_fat(fat16_ins,ClusterN,FatClusEntryVal);
            }
            ClusterN = FatClusEntryVal;
            FatClusEntryVal =  fat_entry_by_cluster(fat16_ins, ClusterN);
            temp++;
        }
        FirstSectorofCluster = ((ClusterN - 2) * fat16_ins->Bpb.BPB_SecPerClus) + fat16_ins->FirstDataSector ;
        sector_read(FirstSectorofCluster+SecNum, sector_buffer);
        temp = BYTES_PER_SECTOR - SecOffset;
        if(temp > size)
        {
            memcpy(sector_buffer+SecOffset,buffer,size);
            sector_write(FirstSectorofCluster+SecNum, sector_buffer);
        }
        else
        {
            memcpy(sector_buffer+SecOffset,buffer,temp);
            total = temp;
            sector_write(FirstSectorofCluster+SecNum, sector_buffer);
            SecNum++;
            while(total < size)
            {
                if(SecNum == fat16_ins->Bpb.BPB_SecPerClus)//整个簇都读完了
                {
                    ClusterNum = FatClusEntryVal;
                    if(ClusterNum == 0xffff)//空的
                    {
                        ClusterNum = find_empty_fat();//找个新的簇
                        change_fat(fat16_ins,ClusterN,ClusterNum);
                    }
                    ClusterN = ClusterNum;
                    first_sector_by_cluster(fat16_ins,ClusterN,&FatClusEntryVal,&FirstSectorofCluster,sector_buffer);
                    SecNum = 1;
                }
                else
                {
                    sector_read(FirstSectorofCluster+SecNum, sector_buffer);
                    SecNum++;
                }
                temp = (size - total > BYTES_PER_SECTOR)?BYTES_PER_SECTOR:(size - total);
                memcpy(sector_buffer,buffer+total,temp);
                sector_write(FirstSectorofCluster+SecNum-1, sector_buffer);
                total +=temp;
            }
        }
        //改变目录项
        Dir.DIR_FileSize +=size;
        sector_read(DirSecNum, sector_buffer);
        memcpy(sector_buffer+32*DirNum,&Dir,32);
        sector_write(DirSecNum, sector_buffer);
        return size;
    }
    return 0;
}
int fat16_mknod(const char *path, mode_t mode,dev_t dev)
{
    FAT16 *fat16_ins;
    BYTE sector_buffer[BYTES_PER_SECTOR],sector_buffer_temp[BYTES_PER_SECTOR];
    int pathDepth;
    int i,j,flag,len,LongDirCnt;
    int RootDirCnt = 1;
    int DirSecCnt = 1;
    char **paths;
    char *pathc;
    char *temp_path;
    struct fuse_context *context;
    WORD ClusterN, FatClusEntryVal, FirstSectorofCluster,SecNum;
    context = fuse_get_context();
    LDIR_ENTRY LDir[10];
    DIR_ENTRY Dir;
    //返回指针，改变
    fat16_ins = (FAT16 *)context->private_data;
    paths = path_split((char *)path, &pathDepth);
    if (pathDepth==1)
    {
        //在根目录创建
        flag = 1;
        pathc = path_format(paths[0], 1);//1是暂时设置的，目的只是为了生成一个可以用来比对的字符串
        sector_read(fat16_ins->FirstRootDirSecNum, sector_buffer);
        for (i = 1; i <= fat16_ins->Bpb.BPB_RootEntCnt; i++)
        {
            j=(i-1)%(BYTES_PER_SECTOR/32);
            if(j==0&&i>1)//读完了一个扇区
            {
                memcpy(sector_buffer_temp,sector_buffer,BYTES_PER_SECTOR);
                sector_read(fat16_ins->FirstRootDirSecNum+RootDirCnt, sector_buffer);
                RootDirCnt++;
            }
            if(sector_buffer[32*j]==0x00)//发现了一个空的目录项，插进去
            {
                free(pathc);
                pathc = path_format(paths[0], flag);
                Create_shortname_Dir(pathc,0x00 , &Dir);
                Create_longname_Dir(paths[0], pathc, LDir, &LongDirCnt);
                //这里没有考虑根目录项数不够的情况
                //未考虑边界条件，就是总目录项数如果多于当前扇区空余目录项数
                if((j+LongDirCnt)<BYTES_PER_SECTOR/32)
                {
                    memcpy(sector_buffer+32*(j+LongDirCnt),&Dir,32);
                    LongDirCnt--;
                    flag=0;
                }
                else
                {
                    sector_read(fat16_ins->FirstRootDirSecNum+RootDirCnt,sector_buffer_temp);//取下一个扇区
                    i = (j+LongDirCnt)%BYTES_PER_SECTOR/32;
                    LongDirCnt = LongDirCnt-i-1;
                    memcpy(sector_buffer_temp+32*i,&Dir,32);
                    i--;
                    flag=0;
                    while(i>=0)
                    {
                        memcpy(sector_buffer_temp+32*i,&LDir[flag],32);
                        flag++;
                        i--;
                    }
                    sector_write(fat16_ins->FirstRootDirSecNum+RootDirCnt, sector_buffer_temp);
                }
                while(LongDirCnt>=0)
                {
                    memcpy(sector_buffer+32*(j+LongDirCnt),&LDir[flag],32);
                    LongDirCnt--;
                    flag++;
                }
                sector_write(fat16_ins->FirstRootDirSecNum+RootDirCnt-1, sector_buffer);
                //free掉
                free(pathc);
                for(i = 0;i<pathDepth;i++)
                {
                    free(paths[i]);
                }
                free(paths);
                return 0;
            }
            //先找有没有空的位置，没有再覆盖写过但是删除的（todo）
            else if(sector_buffer[32*j+11]!=0x0f&&strncmp(sector_buffer+32*j, pathc,6)==0)//不是长文件名目录项且名字相同
            {
                flag++;
            }
        }
    }
    else
    {
        temp_path = malloc((strlen(path)+1));
        strcpy(temp_path,path);
        pathc = temp_path;
        i=pathDepth;
        while(i>1)
        {
            pathc++;
            if(*pathc=='/')//因为path_split会改变path，所以需要还原
            {
                i--;
            }
        }
        *pathc = '\0';
        if(find_root(fat16_ins, &Dir, temp_path,&ClusterN,&i)==0)
        {
            flag = 1;
            pathc = path_format(paths[pathDepth-1], 1);//1是暂时设置的，目的只是为了生成一个可以用来比对的字符串
            ClusterN = Dir.DIR_FstClusLO;
            first_sector_by_cluster(fat16_ins, ClusterN, &FatClusEntryVal, &FirstSectorofCluster, sector_buffer);
            i = 1;
            j = 0;
            while(sector_buffer[32*j]!=0x00)
            {
                if(sector_buffer[32*j+11]!=0x0f&&strncmp(sector_buffer+32*j, pathc,6)==0)//不是长文件名目录项且名字相同
                {
                    flag++;
                }
                i++;
                j=(i-1)%(BYTES_PER_SECTOR/32);
                if(j==0&&i>1)//读完了一个扇区
                {
                    memcpy(sector_buffer_temp,sector_buffer,BYTES_PER_SECTOR);
                    if(DirSecCnt == fat16_ins->Bpb.BPB_SecPerClus)//读完了整个簇
                    {
                        if(FatClusEntryVal == 0xffff)//空的
                        {
                            FatClusEntryVal = find_empty_fat();//找个新的簇
                            change_fat(fat16_ins,ClusterN,FatClusEntryVal);
                        }
                        ClusterN = FatClusEntryVal; first_sector_by_cluster(fat16_ins,ClusterN,&FatClusEntryVal,&FirstSectorofCluster,sector_buffer);
                        DirSecCnt = 1;
                    }
                    else
                    {
                        sector_read(FirstSectorofCluster+DirSecCnt, sector_buffer);
                        DirSecCnt++;
                    }
                }
            }
            free(pathc);
            pathc = path_format(paths[pathDepth-1], flag);
            Create_shortname_Dir(pathc,0x00 , &Dir);
            Create_longname_Dir(paths[pathDepth-1], pathc, LDir, &LongDirCnt);
            //这里没有考虑根目录项数不够的情况
            //未考虑边界条件，就是总目录项数如果多于当前扇区空余目录项数
            if((j+LongDirCnt)<BYTES_PER_SECTOR/32)
            {
                memcpy(sector_buffer+32*(j+LongDirCnt),&Dir,32);
                LongDirCnt--;
                flag=0;
            }
            else
            {
                if(DirSecCnt<fat16_ins->Bpb.BPB_SecPerClus)
                {
                    sector_read(FirstSectorofCluster+DirSecCnt,sector_buffer_temp);//取下一个扇区
                    SecNum = FirstSectorofCluster+DirSecCnt;
                }
                else
                {
                    if(FatClusEntryVal == 0xffff)
                    {
                        FatClusEntryVal = find_empty_fat();
                        change_fat(fat16_ins,ClusterN,FatClusEntryVal);
                    }
                    ClusterN = FatClusEntryVal;
                    first_sector_by_cluster(fat16_ins,ClusterN,&FatClusEntryVal,&SecNum,sector_buffer_temp);
                }
                i = (j+LongDirCnt)%BYTES_PER_SECTOR/32;
                LongDirCnt = LongDirCnt-i-1;
                memcpy(sector_buffer_temp+32*i,&Dir,32);
                i--;
                flag=0;
                while(i>=0)
                {
                    memcpy(sector_buffer_temp+32*i,&LDir[flag],32);
                    flag++;
                    i--;
                }
                sector_write(SecNum,sector_buffer_temp);
            }
            while(LongDirCnt>=0)
            {
                memcpy(sector_buffer+32*(j+LongDirCnt),&LDir[flag],32);
                LongDirCnt--;
                flag++;
            }
            sector_write(FirstSectorofCluster+DirSecCnt-1, sector_buffer);
            //free掉
            free(pathc);
            for(i = 0;i<pathDepth;i++)
            {
                free(paths[i]);
            }
            free(paths);
            free(temp_path);
            return 0;
        }
    }
    for(i = 0;i<pathDepth;i++)
    {
        free(paths[i]);
    }
    free(paths);
    free(temp_path);
    return 0;
}
int fat16_utimens(const char* path,const struct timespec tv[2])
{
    FAT16 *fat16_ins;
    struct fuse_context *context;
    context = fuse_get_context();
    fat16_ins = (FAT16 *)context->private_data;
    DIR_ENTRY Dir;
    int DirNum;
    WORD DirSecNum;
    BYTE sector_buffer[BYTES_PER_SECTOR];
    int i;
    time_t t;
    if(find_root(fat16_ins, &Dir, path,&DirSecNum,&DirNum)==0)
    {
        for(i=0;i<2;i++)
        {
            t = tv[i].tv_sec;
	    int days_per_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            int days_per_month_leap[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            int year = 1970;
            int month = 0;
            int day = 0;
            int hours = 0;
            int min = 0;
            int sec = 0;
            int secs_year = DAYS * 365;
            int *dpm = days_per_month;
            while (t) {
                if (t >= secs_year) {
                    year++;
                    t -= secs_year;
                    
                    if (!(year % 400 && (year % 100 == 0 || (year & 3)))) {
                        secs_year = DAYS * 366;
                        dpm = days_per_month_leap;
                    } else {
                        secs_year = DAYS * 365;
                        dpm = days_per_month;
                    }
                } else {
                    if (t >= dpm[month] * DAYS) {
                        t -= dpm[month] * DAYS;
                        month++;
                    } else {
                        day = t / DAYS;
                        t -= day * DAYS;
                        hours = t / HOURS;
                        t -= hours * HOURS;
                        min = t / MINUTES;
                        t -= min * MINUTES;
                        sec = t;
                        t = 0;
                    }
                }
            }
            if(i == 0)
            {
                Dir.DIR_LstAccDate = ((year - 1980)<<9)|((month+1)<<5)|(day+1);
            }
            else
            {
                Dir.DIR_WrtTime = ((hours)<<11)|((min)<<5)|(sec/2);
                Dir.DIR_WrtDate = ((year - 1980)<<9)|((month+1)<<5)|(day+1);
            }
        }
        sector_read(DirSecNum, sector_buffer);
        memcpy(sector_buffer+32*DirNum,&Dir,32);
        sector_write(DirSecNum, sector_buffer);
    }
    return 0;
}
//成功返回1
/*
int fat16_rename(const char* path, const char *newpath)//为了省事没改成oldpath
{
    FAT16 *fat16_ins;
    struct fuse_context *context;
    context = fuse_get_context();
    fat16_ins = (FAT16 *)context->private_data;
    DIR_ENTRY Dir;
    int DirNum,k,m,LongDirCnt;
    unsigned char chksum;
    WORD TempSecNum,ClusterN;
    BYTE sector_buffer[BYTES_PER_SECTOR],sector_buffer_temp[BYTES_PER_SECTOR];
    int temp,pathDepth,i,j;
    char shortname[11];
    char **paths;
    char **patht;
    char *temp_path,pathc;
    LDIR_ENTRY LDir[10];
    paths = path_split((char *)path, &pathDepth);
    patht = path_split((char *)newpath, &i);
    if(pathDepth==1)
    {
        sector_read(fat16_ins->FirstRootDirSecNum, sector_buffer);
        for (i = 1; i <= fat16_ins->Bpb.BPB_RootEntCnt; i++)
        {
            j=(i-1)%(BYTES_PER_SECTOR/32);
            if(j==0&&i>1)//读完了一个扇区
            {
                memcpy(sector_buffer_temp,sector_buffer,BYTES_PER_SECTOR);
                sector_read(fat16_ins->FirstRootDirSecNum+RootDirCnt, sector_buffer);
                RootDirCnt++;
            }
            if(sector_buffer[32*j] == 0x00)//后面没有目录了！
                return 1;
            if(sector_buffer[32*j+11] == 0xf||sector_buffer[32*j] == 0xe5)
                continue;
            //能到这边说明是一个短目录项
            memcpy(shortname,buffer+32*j,11);
            memcpy(&ClusterN,buffer+32*j+26,2);
            name = get_long_filename(j,sector_buffer,sector_buffer_temp,shortname);
            if(strcmp(name,paths[0])==0)
            {
                memcpy(&Dir,buffer+32*j,32);
                temp = strlen(name)+1;
                temp = (temp+13-1)/13+1;//求出所有的长目录项
                Create_longname_Dir(patht[0], pathc, LDir, &LongDirCnt);
                while(j>=0&&temp>0)
                {
                    sector_buffer[32*j] = 0xe5;
                    temp--;
                    j--;
                }
                sector_write(fat16_ins->FirstRootDirSecNum+RootDirCnt-1, sector_buffer);
                if(temp>0)
                {
                    i=1;
                    while(temp>0)
                    {
                        sector_buffer_temp[BYTES_PER_SECTOR-32*i] = 0xe5;
                        temp--;
                        i++;
                    }
                    sector_write(fat16_ins->FirstRootDirSecNum+RootDirCnt-2, sector_buffer_temp);
                }
                for(i = 0;i<pathDepth;i++)
                {
                    free(paths[i]);
                }
                free(paths);
                return 0;
            }
        }
    }
    else
    {
        temp_path = malloc((strlen(path)+1));
        strcpy(temp_path,path);
        pathc = temp_path;
        i=pathDepth;
        while(i>1)
        {
            pathc++;
            if(*pathc=='/')//因为path_split会改变path，所以需要还原
            {
                i--;
            }
        }
        *pathc = '\0';
        if(find_root(fat16_ins, &Dir, temp_path,&ClusterN,&i)==0)
        {
            ClusterN = Dir.DIR_FstClusLO;
            first_sector_by_cluster(fat16_ins, ClusterN, &FatClusEntryVal, &FirstSectorofCluster, sector_buffer);
            i = 1;
            j = 0;
            while(sector_buffer[32*j]!=0x00)
            {
                if(sector_buffer[32*j+11]!=0x0f)
                {
                    memcpy(shortname,buffer+32*j,11);
                    name = get_long_filename(j,buffer,buffer_cache,shortname);
                    if(strcmp(name,paths[pathDepth-1])==0)
                    {
                        memcpy(&ClusterN,buffer+32*j+26,2);
                        temp = strlen(name)+1;
                        temp = (temp+13-1)/13+1;//求出所有的长目录项
                        while(j>=0&&temp>0)
                        {
                            sector_buffer[32*j] = 0xe5;
                            temp--;
                            j--;
                        }
                        sector_write(FirstSectorofCluster+DirSecCnt-1, sector_buffer);
                        if(temp>0)
                        {
                            j=1;
                            while(temp>0)
                            {
                                sector_buffer_temp[BYTES_PER_SECTOR-32*j] = 0xe5;
                                temp--;
                                j++;
                            }
                            sector_write(TempSecNum, sector_buffer_temp);
                        }
                        //进入文件夹内搜索所有的文件，将所有的分配的簇号清空
                        clear_dir(ClusterN);
                        clear_fat(ClusterN);
                        for(i = 0;i<pathDepth;i++)
                        {
                            free(paths[i]);
                        }
                        free(paths);
                        return 0;
                    }
                }
                i++;
                j=(i-1)%(BYTES_PER_SECTOR/32);
                if(j==0&&i>1)//读完了一个扇区
                {
                    TempSecNum = FirstSectorofCluster + DirSecCnt -1;
                    memcpy(sector_buffer_temp,sector_buffer,BYTES_PER_SECTOR);
                    if(DirSecCnt == fat16_ins->Bpb.BPB_SecPerClus)//读完了整个簇
                    {
                        ClusterN = FatClusEntryVal;
                        if(ClusterN == 0xffff)//没了，不用读了
                            return 0; first_sector_by_cluster(fat16_ins,ClusterN,&FatClusEntryVal,&FirstSectorofCluster,sector_buffer);
                        DirSecCnt = 1;
                    }
                    else
                    {
                        sector_read(FirstSectorofCluster+DirSecCnt, sector_buffer);
                        DirSecCnt++;
                    }
                }
            }
            free(pathc);
        }
    }
    for(i = 0;i<pathDepth;i++)
    {
        free(paths[i]);
    }
    free(paths);
    return 0;
}
 */
void clear_fat(WORD ClusterN)
{
    FAT16 *fat16_ins;
    struct fuse_context *context;
    context = fuse_get_context();
    fat16_ins = (FAT16 *)context->private_data;
    BYTE sector_buffer[BYTES_PER_SECTOR];
    WORD ThisFATSecNum,ThisFATEntOffset,FATOffset,Cluster;
    FATOffset = ClusterN *2;
    ThisFATSecNum = fat16_ins->Bpb.BPB_RsvdSecCnt + (FATOffset / fat16_ins->Bpb.BPB_BytsPerSec);
    ThisFATEntOffset = FATOffset % fat16_ins->Bpb.BPB_BytsPerSec;
    sector_read(ThisFATSecNum,sector_buffer);
    Cluster = *((WORD*)&sector_buffer[ThisFATEntOffset]);
    *((WORD*)&sector_buffer[ThisFATEntOffset]) = 0x0000;
    sector_write(ThisFATSecNum,sector_buffer);
    if(Cluster == 0xffff)///最后一个
	;
    else
        clear_fat(Cluster);
}
void clear_dir(WORD Cluster)
{
    FAT16 *fat16_ins;
    struct fuse_context *context;
    context = fuse_get_context();
    fat16_ins = (FAT16 *)context->private_data;
    WORD FatClusEntryVal, FirstSectorofCluster,ClusterN;
    ClusterN = Cluster;
    BYTE sector_buffer[BYTES_PER_SECTOR];
    first_sector_by_cluster(fat16_ins, ClusterN, &FatClusEntryVal, &FirstSectorofCluster, sector_buffer);
    int i,j;
    int DirSecCnt = 1;
    //前两个不用管
    i = 3;
    j = 2;
    while(sector_buffer[32*j]!=0x00)
    {
	if(sector_buffer[32*j]!=0xe5)        
	{
	if(sector_buffer[32*j+11]==ATTR_DIRECTORY)
            clear_dir(*((WORD*)&sector_buffer[32*j+26]));
        else if(sector_buffer[32*j+11]!=0x0f)
            clear_fat(*((WORD*)&sector_buffer[32*j+26]));
	}
        i++;
        j=(i-1)%(BYTES_PER_SECTOR/32);
        if(j==0&&i>1)//读完了一个扇区
        {
            if(DirSecCnt == fat16_ins->Bpb.BPB_SecPerClus)//读完了整个簇
            {
                ClusterN = FatClusEntryVal;
                if(ClusterN == 0xffff)//没了，不用读了
                    return ;
            first_sector_by_cluster(fat16_ins,ClusterN,&FatClusEntryVal,&FirstSectorofCluster,sector_buffer);
                DirSecCnt = 1;
            }
            else
            {
                sector_read(FirstSectorofCluster+DirSecCnt, sector_buffer);
                DirSecCnt++;
            }
        }
    }
}
int fat16_unlink(const char* path)
{
    FAT16 *fat16_ins;
    struct fuse_context *context;
    context = fuse_get_context();
    fat16_ins = (FAT16 *)context->private_data;
    DIR_ENTRY Dir;
    int DirNum,k,m;
    unsigned char chksum;
    WORD TempSecNum;
    BYTE sector_buffer[BYTES_PER_SECTOR],sector_buffer_temp[BYTES_PER_SECTOR];
    int temp,pathDepth,i,j;
    char shortname[11];
    char **paths;
    char *temp_path;
    char *pathc,*name;
    int RootDirCnt = 1;
    int DirSecCnt = 1;
    WORD ClusterN, FatClusEntryVal, FirstSectorofCluster;
    paths = path_split((char *)path, &pathDepth);
    if(pathDepth==1)
    {
        sector_read(fat16_ins->FirstRootDirSecNum, sector_buffer);
        for (i = 1; i <= fat16_ins->Bpb.BPB_RootEntCnt; i++)
        {
            j=(i-1)%(BYTES_PER_SECTOR/32);
            if(j==0&&i>1)//读完了一个扇区
            {
                memcpy(sector_buffer_temp,sector_buffer,BYTES_PER_SECTOR);
                sector_read(fat16_ins->FirstRootDirSecNum+RootDirCnt, sector_buffer);
                RootDirCnt++;
            }
            if(sector_buffer[32*j] == 0x00)//后面没有目录了！
                return 1;
            if(sector_buffer[32*j+11] == 0xf||sector_buffer[32*j] == 0xe5)
                continue;
            //能到这边说明是一个短目录项
            memcpy(shortname,sector_buffer+32*j,11);
            memcpy(&ClusterN,sector_buffer+32*j+26,2);
            name = get_long_filename(j,sector_buffer,sector_buffer_temp,shortname);
            if(strcmp(name,paths[0])==0)
            {
                temp = strlen(name)+1;
                temp = (temp+13-1)/13+1;//求出所有的长目录项
                
                while(j>=0&&temp>0)
                {
                    sector_buffer[32*j] = 0xe5;
                    temp--;
                    j--;
                }
                sector_write(fat16_ins->FirstRootDirSecNum+RootDirCnt-1, sector_buffer);
                if(temp>0)
                {
                    j=1;
                    while(temp>0)
                    {
                        sector_buffer_temp[BYTES_PER_SECTOR-32*j] = 0xe5;
                        temp--;
                        j++;
                    }
                    sector_write(fat16_ins->FirstRootDirSecNum+RootDirCnt-2, sector_buffer_temp);
                }
                //进入文件夹内搜索所有的文件，将所有的分配的簇号清空
                clear_fat(ClusterN);
                for(i = 0;i<pathDepth;i++)
                {
                    free(paths[i]);
                }
                free(paths);
                return 0;
            }
        }
    }
    else
    {
        temp_path = malloc((strlen(path)+1));
        strcpy(temp_path,path);
        pathc = temp_path;
        i=pathDepth;
        while(i>1)
        {
            pathc++;
            if(*pathc=='/')//因为path_split会改变path，所以需要还原
            {
                i--;
            }
        }
        *pathc = '\0';
        if(find_root(fat16_ins, &Dir, temp_path,&ClusterN,&i)==0)
        {
            ClusterN = Dir.DIR_FstClusLO;
            first_sector_by_cluster(fat16_ins, ClusterN, &FatClusEntryVal, &FirstSectorofCluster, sector_buffer);
            i = 1;
            j = 0;
            while(sector_buffer[32*j]!=0x00)
            {
                if(sector_buffer[32*j+11]!=0x0f)
                {
                    memcpy(shortname,sector_buffer+32*j,11);
                    name = get_long_filename(j,sector_buffer,sector_buffer_temp,shortname);
                    if(strcmp(name,paths[pathDepth-1])==0)
                    {
                        memcpy(&ClusterN,sector_buffer+32*j+26,2);
                        temp = strlen(name)+1;
                        temp = (temp+13-1)/13+1;//求出所有的长目录项
                        while(j>=0&&temp>0)
                        {
                            sector_buffer[32*j] = 0xe5;
                            temp--;
                            j--;
                        }
                        sector_write(FirstSectorofCluster+DirSecCnt-1, sector_buffer);
                        if(temp>0)
                        {
                            j=1;
                            while(temp>0)
                            {
                                sector_buffer_temp[BYTES_PER_SECTOR-32*j] = 0xe5;
                                temp--;
                                j++;
                            }
                            sector_write(TempSecNum, sector_buffer_temp);
                        }
                        //进入文件夹内搜索所有的文件，将所有的分配的簇号清空
                        clear_fat(ClusterN);
                        for(i = 0;i<pathDepth;i++)
                        {
                            free(paths[i]);
                        }
                        free(paths);
                        return 0;
                    }
                }
                i++;
                j=(i-1)%(BYTES_PER_SECTOR/32);
                if(j==0&&i>1)//读完了一个扇区
                {
                    TempSecNum = FirstSectorofCluster + DirSecCnt -1;
                    memcpy(sector_buffer_temp,sector_buffer,BYTES_PER_SECTOR);
                    if(DirSecCnt == fat16_ins->Bpb.BPB_SecPerClus)//读完了整个簇
                    {
                        ClusterN = FatClusEntryVal;
                        if(ClusterN == 0xffff)//没了，不用读了
                            return 0; first_sector_by_cluster(fat16_ins,ClusterN,&FatClusEntryVal,&FirstSectorofCluster,sector_buffer);
                        DirSecCnt = 1;
                    }
                    else
                    {
                        sector_read(FirstSectorofCluster+DirSecCnt, sector_buffer);
                        DirSecCnt++;
                    }
                }
            }
            free(pathc);
        }
    }
    for(i = 0;i<pathDepth;i++)
    {
        free(paths[i]);
    }
    free(paths);
    return 0;
}
int fat16_truncate(const char* path,off_t size)
{
    //todo
    return 0;
}
int fat16_rmdir(const char* path)
{
    FAT16 *fat16_ins;
    struct fuse_context *context;
    context = fuse_get_context();
    fat16_ins = (FAT16 *)context->private_data;
    DIR_ENTRY Dir;
    int DirNum,k,m;
    unsigned char chksum;
    WORD TempSecNum;
    BYTE sector_buffer[BYTES_PER_SECTOR],sector_buffer_temp[BYTES_PER_SECTOR];
    int temp,pathDepth,i,j;
    char shortname[11];
    char **paths;
    char *temp_path;
    char *pathc,*name;
    int RootDirCnt = 1;
    int DirSecCnt = 1;
    WORD ClusterN, FatClusEntryVal, FirstSectorofCluster;
    paths = path_split((char *)path, &pathDepth);
    if(pathDepth==1)
    {
        sector_read(fat16_ins->FirstRootDirSecNum, sector_buffer);
        for (i = 1; i <= fat16_ins->Bpb.BPB_RootEntCnt; i++)
        {
            j=(i-1)%(BYTES_PER_SECTOR/32);
            if(j==0&&i>1)//读完了一个扇区
            {
                memcpy(sector_buffer_temp,sector_buffer,BYTES_PER_SECTOR);
                sector_read(fat16_ins->FirstRootDirSecNum+RootDirCnt, sector_buffer);
                RootDirCnt++;
            }
            if(sector_buffer[32*j] == 0x00)//后面没有目录了！
                return 1;
            if(sector_buffer[32*j+11] == 0xf||sector_buffer[32*j] == 0xe5)
                continue;
            //能到这边说明是一个短目录项
            memcpy(shortname,sector_buffer+32*j,11);
            memcpy(&ClusterN,sector_buffer+32*j+26,2);
            name = get_long_filename(j,sector_buffer,sector_buffer_temp,shortname);
            if(strcmp(name,paths[0])==0)
            {
                temp = strlen(name)+1;
                temp = (temp+13-1)/13+1;//求出所有的长目录项
                
                while(j>=0&&temp>0)
                {
                    sector_buffer[32*j] = 0xe5;
                    temp--;
                    j--;
                }
                sector_write(fat16_ins->FirstRootDirSecNum+RootDirCnt-1, sector_buffer);
                if(temp>0)
                {
                    j=1;
                    while(temp>0)
                    {
                        sector_buffer_temp[BYTES_PER_SECTOR-32*j] = 0xe5;
                        temp--;
                        j++;
                    }
                    sector_write(fat16_ins->FirstRootDirSecNum+RootDirCnt-2, sector_buffer_temp);
                }
                //进入文件夹内搜索所有的文件，将所有的分配的簇号清空
                clear_dir(ClusterN);
                clear_fat(ClusterN);
                for(i = 0;i<pathDepth;i++)
                {
                    free(paths[i]);
                }
                free(paths);
                return 0;
            }
        }
    }
    else
    {
        temp_path = malloc((strlen(path)+1));
        strcpy(temp_path,path);
        pathc = temp_path;
        i=pathDepth;
        while(i>1)
        {
            pathc++;
            if(*pathc=='/')//因为path_split会改变path，所以需要还原
            {
                i--;
            }
        }
        *pathc = '\0';
        if(find_root(fat16_ins, &Dir, temp_path,&ClusterN,&i)==0)
        {
            ClusterN = Dir.DIR_FstClusLO;
            first_sector_by_cluster(fat16_ins, ClusterN, &FatClusEntryVal, &FirstSectorofCluster, sector_buffer);
            i = 1;
            j = 0;
            while(sector_buffer[32*j]!=0x00)
            {
                if(sector_buffer[32*j+11]!=0x0f)
                {
                    memcpy(shortname,sector_buffer+32*j,11);
                    name = get_long_filename(j,sector_buffer,sector_buffer_temp,shortname);
                    if(strcmp(name,paths[pathDepth-1])==0)
                    {
                        memcpy(&ClusterN,sector_buffer+32*j+26,2);
                        temp = strlen(name)+1;
                        temp = (temp+13-1)/13+1;//求出所有的长目录项
                        while(j>=0&&temp>0)
                        {
                            sector_buffer[32*j] = 0xe5;
                            temp--;
                            j--;
                        }
                        sector_write(FirstSectorofCluster+DirSecCnt-1, sector_buffer);
                        if(temp>0)
                        {
                            j=1;
                            while(temp>0)
                            {
                                sector_buffer_temp[BYTES_PER_SECTOR-32*j] = 0xe5;
                                temp--;
                                j++;
                            }
                            sector_write(TempSecNum, sector_buffer_temp);
                        }
                        //进入文件夹内搜索所有的文件，将所有的分配的簇号清空
                        clear_dir(ClusterN);
                        clear_fat(ClusterN);
                        for(i = 0;i<pathDepth;i++)
                        {
                            free(paths[i]);
                        }
                        free(paths);
                        return 0;
                    }
                }
                i++;
                j=(i-1)%(BYTES_PER_SECTOR/32);
                if(j==0&&i>1)//读完了一个扇区
                {
                    TempSecNum = FirstSectorofCluster + DirSecCnt -1;
                    memcpy(sector_buffer_temp,sector_buffer,BYTES_PER_SECTOR);
                    if(DirSecCnt == fat16_ins->Bpb.BPB_SecPerClus)//读完了整个簇
                    {
                        ClusterN = FatClusEntryVal;
                        if(ClusterN == 0xffff)//没了，不用读了
                            return 0; first_sector_by_cluster(fat16_ins,ClusterN,&FatClusEntryVal,&FirstSectorofCluster,sector_buffer);
                        DirSecCnt = 1;
                    }
                    else
                    {
                        sector_read(FirstSectorofCluster+DirSecCnt, sector_buffer);
                        DirSecCnt++;
                    }
                }
            }
            free(temp_path);
        }
    }
    for(i = 0;i<pathDepth;i++)
    {
        free(paths[i]);
    }
    free(paths);
    return 0;
}
struct fuse_operations fat16_oper = {
    .init = fat16_init,
    .destroy = fat16_destroy,
    .getattr = fat16_getattr,
    .readdir = fat16_readdir,
    .read = fat16_read,
    .mkdir = fat16_mkdir,
    .write = fat16_write,
    .utimens = fat16_utimens,
     .rmdir = fat16_rmdir,
    /*
     .rename = fat16_rename,
     */
    .mknod = fat16_mknod,
     .unlink = fat16_unlink,
    /*
     .truncate = fat16_truncate
     */
};

int main(int argc, char *argv[])
{
    int ret;
    
    FAT16 *fat16_ins = pre_init_fat16();
    
    ret = fuse_main(argc, argv, &fat16_oper, fat16_ins);
    
    return ret;
}

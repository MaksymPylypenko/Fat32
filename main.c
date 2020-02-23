// author:            Pylypenko Maksym 7802672
// last change:       2018-July-13

#include <stdio.h> // default
#include <stdlib.h> // sizeof
#include <unistd.h> // lseek
#include <sys/types.h> // lseek
#include <fcntl.h> // open
#include <string.h> // strcopy

#include "fat32.h" // structs 

fat32BS * BPB;
FSInfo * info;
uint32_t dataSector;
uint32_t fatSector;
int image;
char * fileName;
int found = 0;

// methods
void readSector(int cluster,int depth);
void fetch(int cluster,char * path);
void spitOutFile(int cluster);
int askFAT(int cluster);
void print_info();
void stripEntryName(DIREntry * entry);
int validateFile(DIREntry * entry);
int getNextCluster(DIREntry * entry);



// ------------------- MAIN -------------------
int main( int argc, char *argv[] ) {     
  // For simplicity purposes, I assume:
  // 1. Each cluster has 1 sector that is 512 bits 
  // 2. Each entry is 32 bits 
  
  // default values
  char * path = "FOLDER1/FOLDER2/FOLDER3/THEFILE.TXT";
  fileName = "a4image";
  
  if( argc > 0 ) 
  {
	fileName = argv[1]; // voume name
  }
    
  BPB = malloc(512);    
  info = malloc(512);   
  
  image = open(fileName,O_RDONLY);  
	  
  // Reserved Area
  read(image,BPB,512);  
  read(image,info,512);
  
  // FAT Area     
  fatSector = BPB->BPB_RsvdSecCnt;

  // Data Area
  dataSector = fatSector + (BPB->BPB_NumFATs * BPB->BPB_FATSz32 );   
  int root = BPB->BPB_RootClus; 

  
  if ( argc > 1 ) 
  {
	 if(strcmp(argv[2],"info")==0)
	 {
		print_info();
	 }
	 else if(strcmp(argv[2],"list")==0)
	 {
	    //Recursively read the root sector   
		readSector(root,0);
	 }
	 else if(strcmp(argv[2],"get")==0)
	 {
		if( argc > 2)
		{
			path = argv[3];
			
			//Recursively read the root sector   
			fetch(root,path);
		}
	 }	 
  }    
  
  
  close(image);     
  return 0;
}


// ------------------- CORE METHODS -------------------

// Recursively reads sector 
// Note, that we need the first cluster to properly read a sector  
void readSector(int cluster,int depth)
{    
  uint32_t sector = ((cluster - 2) * BPB->BPB_SecPerClus) + dataSector;
  // if(depth==0) { printf("rootSector: %u\n",sector); }
  
  int currImage = open(fileName,O_RDONLY);     
  lseek(currImage,sector*512,SEEK_SET); // select offset 

  DIREntry * entry = malloc(32);  
  int i,j;  
  
  for(i=0; i<16; i++) // 512/32 = 16
  {    
    read(currImage,entry,32);  
    // first condition selects folders only
    // second condition ignores free directory entries (-27 is 0xE5)
    // third condition ignores "return" folder
    if(entry->DIR_Attr == 0x10 && entry->DIR_Name[0] != -27 && entry->DIR_Name[0] != '.' ) 
    {      
      for(j=-1;j<depth;j++)
      {
        printf("   ");
      }
      printf("/%.8s\n",entry->DIR_Name); // don't print extension    
     
	  uint32_t nextCluster = getNextCluster(entry);
	  
	  // open folder 
      readSector(nextCluster,depth+1);       
    }
    // special case for the root entry
    else if(entry->DIR_Attr == 0x08)
    {
      printf(".%.8s\n",entry->DIR_Name);
    } 
	// this is suppose to be a valid file ... 
	// filtering ATTR_LONG_NAME
    // make sure the first char is in valid ascii range 
    else if( validateFile(entry) )
    {
      for(j=-1;j<depth;j++)
      {
        printf("   ");
      }

      printf("item: %.11s\n",entry->DIR_Name);	  
	  //printf("size: %d\n",entry->DIR_FileSize);  
	  
    }
    else
    {
      // garbage 
    }
  }

  // Are we done, Daddy ? Maybe.  
  int continuation = askFAT(cluster);  
   
  if(continuation != -1)
  {
	readSector(continuation,depth);  
  }   
  
  // Now we are done.
  close(currImage);   
}


// Allows to reach the end of data if it is split between multiple secots 
int askFAT(int cluster)
{
  uint32_t fatOffset = fatSector * 512; 
  uint32_t lineOffset = cluster * 4;
    
  int tempImage = open(fileName,O_RDONLY);  
  lseek(tempImage,fatOffset+lineOffset,SEEK_SET);
  
  FATEntry * line = malloc(4);  
  read(tempImage,line,4);  
    
  line->address = line->address & 0x0FFFFFFF; // mask 00001111111111111111111111111111
  
  if(line->address == 0x0FFFFFFF)							
  {
    //printf(" end-of-chain\n");
  }
  else if(line->address == 0x0FFFFFF7)
  {
	//printf(" bad cluster\n");
	return askFAT((int)line->address); // skip	
  }
  else if(line->address == 0x0)
  {
	//printf(" free\n");	
  }
  else
  {
	// there is more! 
	return (int)line->address;
  }
  close(tempImage);    
  
  return -1;
}


// Prints information about the drive 
void print_info()
{
  uint16_t BytesPerCluster = BPB->BPB_BytesPerSec * BPB->BPB_SecPerClus;
  uint16_t FreeSpace = info->FSI_Free_Count * BytesPerCluster / 1024;
  uint16_t Capacity = BPB->BPB_TotSec32 * BPB->BPB_BytesPerSec / 1024;
  printf("Drive name: %s\n",BPB->BS_OEMName);   
  printf("Size of 1 cluster: %.1f kb\n",(float)BytesPerCluster /1024);   
  printf("Free space on the drive: %u kb\n",FreeSpace);
  printf("Storage capacity : %u kb\n",Capacity);
  printf("\n");
  
  // printf("FSI_LeadSig: 0x%x\n",info->FSI_LeadSig); // expecting 0x4161525  
  // printf("FSI_TrailSig: 0x%x\n",info->FSI_TrailSig); // expecting 0xAA550000 
  // printf("\ndataSector: %u\n",dataSector);  
  
  // printf("BPB_RsvdSecCnt  %d\n",BPB->BPB_RsvdSecCnt);
  // printf("BPB->BPB_NumFATs  %d\n",BPB->BPB_NumFATs);
  // printf("BPB->BPB_FATSz32  %d\n",BPB->BPB_FATSz32);
}


// Fetches item from a file
void fetch(int cluster,char * path)
{    
  // make sure the path is handled properly  
  int i;    
  char folder[20];
  int isFile = 0;
  
  if(path[0] == '/') // do we really need the first char ?
  {
    path++;
  }

  i=0;  
  while(path[i] != 0)
  {    
    i++;
  }
  char originalPath[i];
  strcpy(originalPath,path);
    
  i=0;
  while(path[i] != 0 && path[i] != '/')
  {    
    i++;
  }
  
  if(path[i]==0)
  {
	i=0; 
	while(path[i] != 0 && path[i] != '.') // do we need an extension ?
	{    
		i++;
	}
    isFile = 1; 
	// since I have an extension token, it is possible to handle files differently...
	// however, I will simply poop a file contents to the terminal
  }

  strcpy(folder, path);
  folder[i]= 0;

  path = path + i;

  // printf("%s\n",folder);
  // printf("%s\n",path);
	
  // similar to readSector routine... 
  uint32_t sector = ((cluster - 2) * BPB->BPB_SecPerClus) + dataSector;  
  int currImage = open(fileName,O_RDONLY);    
  lseek(currImage,sector*512,SEEK_SET); // select offset  
    
  DIREntry * entry = malloc(32);  
  int quitRecursion = 0; // set to one if we already entered a folder 
  uint32_t nextCluster;
  
  for(i=0; !found && !quitRecursion && i<16; i++) // 512/32 = 16
  {    
    read(currImage,entry,32);  
    if(isFile==0 && entry->DIR_Attr == 0x10 && entry->DIR_Name[0] != -27 && entry->DIR_Name[0] != '.' ) 
    {   	
	  stripEntryName(entry);
	  
	  if(strcmp(entry->DIR_Name,folder) == 0)
	  {
		nextCluster = getNextCluster(entry);
		
		// open folder 
		printf("%s/ ",entry->DIR_Name);
		// printf("new path %s\n",path);
		fetch(nextCluster,path);   
		
		quitRecursion = 1;
	  }	  
    }   
    else if(isFile && validateFile(entry) )
    {
		char originalName[11];
		strcpy(originalName,entry->DIR_Name);
		stripEntryName(entry);
		if(strcmp(entry->DIR_Name,folder) == 0)
		{
			printf("%.11s:\n\n",originalName);
			nextCluster = getNextCluster(entry);
			spitOutFile(nextCluster);		
			quitRecursion = 1;
			found = 1;
		}	  
    }
  }
	
  if(!quitRecursion) // did we found what we were looking for ? 
  {
	  // Are we done, Daddy ? Maybe.  
	  int continuation = askFAT(cluster);  
   
	  if(continuation != -1)
	  {
		fetch(continuation,originalPath); // path is brocken.... fixed 
	  }   
	  
	  if(!found)
	  {
		printf("Not found\n");
	  }	  
	  // Now we are done.
  }
  close(currImage);  
}


// ------------------- HELPERS -------------------

void spitOutFile(int cluster)
{	
    uint32_t sector = ((cluster - 2) * BPB->BPB_SecPerClus) + dataSector;  
    int currImage = open(fileName,O_RDONLY);    
    lseek(currImage,sector*512,SEEK_SET); // select offset  
	
	uint16_t size = BPB->BPB_BytesPerSec * BPB->BPB_SecPerClus;
	char buf[size];
	read(currImage,buf,size);  	
	printf("%s",buf);
		
	// Are we done, Daddy ? Maybe.  
	int continuation = askFAT(cluster);  

	if(continuation != -1)
	{
	  spitOutFile(continuation); 
	}   	
	
}

int validateFile(DIREntry * entry)
{
	int ret = 0;
	if( entry->DIR_Attr != (0x01 | 0x02 | 0x04 | 0x08) && entry->DIR_Name[0] > 32 && entry->DIR_Name[0] <127 && entry->DIR_Name[0] != '.')
	{
		ret = 1;
	}
	return ret;
}


void stripEntryName(DIREntry * entry)
{
	entry->DIR_Name[8] = '\0'; // remove extension  
	int j=7; 
	while(entry->DIR_Name[j] == ' ') 
	{
	  j--;
	}	  
	entry->DIR_Name[j+1]= '\0'; // strip trailing white spaces  	  
	//printf("changed: %s\n",entry->DIR_Name); 
}


int getNextCluster(DIREntry * entry)
{
	uint32_t  LO,HI;
	LO = entry->DIR_FstClusLO;
	HI = entry->DIR_FstClusHI;        
  
	HI = HI<<16; 
	HI = HI+LO; // merged HI + LO bits
	
	return HI;
}
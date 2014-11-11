#define true 1

void main2();
void main() { main2(); }

struct procTable
{
    int isActive;
    int sp;
} kproctable[8];
int CurrentProcess;

int msgAge[8][8];

void printString(char* str);
void readString(char buffer[]);
void readSector(char* buffer, int sector);
void writeSector(char* buffer, int sector);
int myDIV(int dividend, int divisor);
int myMOD(int dividend, int divisor);
void directory();
void deleteFile(char* filename);
void readFile(char* filename, char* outbuf);
void writeFile(char* filename, char* inbuf);
void handleInterrupt21(int AX, int BX, int CX, int DX);
void executeProgram(char* name);
void terminate();
void killProcess(int procID);
void handleTimerInterrupt(int segment, int sp);

void sendMessage (char * msg, int processNum);
void getMessage (char *msg);
// interrupt() usage:
// int interrupt (int number, int AX, int BX, int CX, int DX)

void main2()
{
    int i,j;
    CurrentProcess = 0;
    for(i=0; i<8; i++)
    {
        kproctable[i].isActive = 0;
        kproctable[i].sp = 0xFF00;
    }

    for(i=0;i<8;i++)
	for(j=0;j<8;j++)
		msgAge[i][j]=0;

    makeInterrupt21();
    makeTimerInterrupt();
    //interrupt(0x21, 9, "tstpr2", 0, 0);
    interrupt(0x21, 9, "shell", 0, 0);
    while(1);
}

void printString(char* str)
{
	int i=0;

	while(*(str+i) != '\0')
	{
		interrupt(0x10, 0xe*256+*(str+i), 0, 0, 0);
		i++;
	}

	// start a new line before printing, use CRLF
	//interrupt(0x10, 0xe*256+0xd, 0, 0, 0);
	//interrupt(0x10, 0xe*256+0xa, 0, 0, 0);
}

void readString(char buffer[])
{
	int j=0;
	while(true)
	{
		char current_read = interrupt(0x16, 0x0, 0, 0, 0);
		// if it's not backspace (0x8) or enter (0xd), save it
		if(current_read != 0xd && current_read != 0x8)
		{
			buffer[j] = current_read;
			j++;
			// display what entered on stdout (console)
			interrupt(0x10, 0xe*256+current_read, 0, 0, 0);
		}
		// if it is backspace (0x8)
		else if(current_read == 0x8)
		{
			// move one character backwards to drop the last enter char.
			j--;
			// backspace will not erase the deleted character on screen
			// use a blank to overwrite that position and backspace again.
			interrupt(0x10, 0xe*256+0x8, 0, 0, 0);
			interrupt(0x10, 0xe*256+' ', 0, 0, 0);
			interrupt(0x10, 0xe*256+0x8, 0, 0, 0);
		}
		else {
			// add CR to buffer end
			buffer[j] = 0xd;
			// add LF to buffer end
			buffer[j+1] = 0xa;
			buffer[j+2] = 0x0;
			break;
		}
	};
}

void readSector(char* buffer, int sector)
{
	int AX, CX, DX;

	AX = 2*256 + 1;
	CX = myDIV(sector, 36)*256 + ( myMOD(sector, 18) + 1 );
	DX = myMOD(myDIV(sector, 18), 2)*256 + 0;
	interrupt(0x13, AX, buffer, CX, DX);
}

void writeSector(char* buffer, int sector)
{
	int AX, CX, DX;

	AX = 3*256 + 1;
	CX = myDIV(sector, 36)*256 + ( myMOD(sector, 18) + 1 );
	DX = myMOD(myDIV(sector, 18), 2)*256 + 0;
	interrupt(0x13, AX, buffer, CX, DX);
}

int myDIV(int dividend, int divisor)
{
	int result =0;

	if(divisor == 0)
		/* can't divide 0, return error */
		return 1;

	/* circle minus */
	while( (dividend -= divisor) >= 0 )
		++result;

	return result;
}

int myMOD(int dividend, int divisor)
{
	int mod_result;
	// use DIV() to get MOD result
	mod_result = dividend - (divisor * myDIV(dividend, divisor));
	return mod_result;
}

void directory()
{
	int p_row, p_col;
	char dirbuf[512];
	// read out the whole sector #2
	readSector(dirbuf, 2);
	// search the beginning of every 32 bytes
	for(p_row=0; p_row<512; p_row+=32)
	{
		if(dirbuf[p_row] != 0)
		{
			// read out the first 6 bytes of each file entry and print
			for(p_col=p_row; p_col<(p_row+6); p_col++)
				interrupt(0x10, 0xe*256 + dirbuf[p_col], 0, 0, 0);

			interrupt(0x10, 0xe*256+0xd, 0, 0, 0);
			interrupt(0x10, 0xe*256+0xa, 0, 0, 0);
		}
	}
}

// deleteFile supports multi-sector storage
void deleteFile(char* filename)
{
	char mapbuf[512], dirbuf[512];
	// use static to initialize with all zero
	static char zerobuf[512];
	int i, j, n;
	// load map and directory into buffer
	readSector(mapbuf, 1);
	readSector(dirbuf, 2);
	for(i=0; i<512; i+=32)
	{
		// search filename in directory
		if(dirbuf[i] == filename[0] && dirbuf[i+1] == filename[1] && \
		   dirbuf[i+2] == filename[2] && dirbuf[i+3] == filename[3] && \
           dirbuf[i+4] == filename[4] && dirbuf[i+5] == filename[5])
		{
			// delete file name in entry in directory
            for(n=i; n<(i+6); n++)
            {
                dirbuf[n] = 0x00;
            }
			// find occupied sector
			for(j=i+6; j<(i+32); j++)
			{
				// erase corresponding sector
				if(dirbuf[j] != 0x00)
				{
					writeSector(zerobuf, dirbuf[j]);
					// update map in sector #1
					mapbuf[dirbuf[j]] = 0x00;
				}
			}
		}
	}
	// write back to update disk
	writeSector(mapbuf, 1);
	writeSector(dirbuf, 2);
}

// readFile supports multi-sector storage
void readFile(char* filename, char* outbuf)
{
	char dirbuf[512];
	int i, j;
    //int m=0;
	// load directory sector into buffer
	readSector(dirbuf, 2);
	for(i=0; i<512; i+=32)
	{
		// search filename in directory
		if(dirbuf[i] == filename[0] && dirbuf[i+1] == filename[1] && \
		   dirbuf[i+2] == filename[2] && dirbuf[i+3] == filename[3] && \
		   dirbuf[i+4] == filename[4] && dirbuf[i+5] == filename[5])
		{
			// find occupied sector
			for(j=i+6; j<(i+32); j++)
			{
				if(dirbuf[j] != 0)
				{
					//readSector((outbuf+m*512), dirbuf[j]);
					//m++;
					readSector((outbuf+(j-i-6)*512), dirbuf[j]);
				}
			}
		}
	}
}

// writeFile supports multi-sector storage
void writeFile(char* filename, char* inbuf)
{
	char mapbuf[512], dirbuf[512];
	int i, j, n;
    int sector_index[26];
    int valid_sector;
	// load map and directory into buffer
	readSector(mapbuf, 1);
	readSector(dirbuf, 2);
    for(i=0; i<13312; i+=512)
    {
        if(inbuf[i] == 0x00)
            valid_sector = myDIV(i, 512);
    }
	// search for available sectors in map
    for(j=0; j<valid_sector; j++)
    {
        for(n=0; n<512; n++)
        {
            if(mapbuf[n] == 0x00)
            {
                mapbuf[n] = 0xFF;
                sector_index[j] = n;
                break;
            }
        }
    }
	// search for available file entry in directory
	for(i=0; i<512; i+=32)
	{
		if(dirbuf[i] == 0)
			break;
	}
	// write filename into directory
	for(j=i; j<(i+6); j++)
		dirbuf[j] = filename[j-i];

    // write occupied sector number to entry
    for(n=0; n<valid_sector; n++)
        dirbuf[j+n] = sector_index[n];

	writeSector(mapbuf, 1);
	writeSector(dirbuf, 2);
	// write data into corresponding sector
    for(i=0; i<valid_sector; i++)
        writeSector((inbuf+i*512), sector_index[i]);
}

void executeProgram(char* name)
{
    char filebuf[4096];
    int segment;
    int i, j;
    readFile(name, filebuf);
    setKernelDataSegment();
    for(i=0; i<8; i++)
    {
        if(kproctable[i].isActive == 0)
            break;
    }
    restoreDataSegment();
    segment = 0x1000 * (i+2);
    //CurrentProcess = i;
    for(j=0; j<4096; j++) {
        putInMemory(segment, j, filebuf[j]);
    }
    // initialize to segment other than kernel
    initializeProgram(segment);
    setKernelDataSegment();
    kproctable[i].isActive = 1;
    restoreDataSegment();
}

//TODO initialize the msgAge for this process back to zero
void killProcess(int procID)
{
    setKernelDataSegment();
    kproctable[procID].isActive = 0;
    kproctable[procID].sp = 0xFF00;
    restoreDataSegment();
}
//TODO initialize the msgAge for this process back to zero
void terminate()
{
    setKernelDataSegment();
    kproctable[ CurrentProcess ].isActive = 0;
    kproctable[ CurrentProcess ].sp = 0xFF00;
    restoreDataSegment();
    while(1);
}

void handleTimerInterrupt(int segment, int sp)
{
int foundActive=0;
	int i=0;
	/*setKernelDataSegment();
	printString("Tic");
	 restoreDataSegment();
*/
	if(segment==0x1000)
	{
		segment=0x2000;
		sp=0xff00;
	}
	


	if(segment!=0x1000)
	{
		setKernelDataSegment();
		kproctable[CurrentProcess].sp=sp;
		restoreDataSegment();
	
		setKernelDataSegment();
	        CurrentProcess++;
	        restoreDataSegment();

	}
	
	setKernelDataSegment();
	while(i<8 && foundActive==0)
	{
		if(kproctable[CurrentProcess].isActive==1)
		{
		//	setKernelDataSegment();
			segment=0x2000+CurrentProcess*0x1000;
		//	restoreDataSegment();

		//	setKernelDataSegment();
			sp=kproctable[CurrentProcess].sp;	
		//	restoreDataSegment();
			
		  foundActive=1;
		}
		else{
		
		CurrentProcess++;
		if(CurrentProcess>7)
		  CurrentProcess=0;
		}
		i++;
	}
	restoreDataSegment();
	returnFromTimer(segment, sp);

}


void sendMessage (char * msg, int processNum)
{
	char msgbuf[100];
	int Pmailbox,offset,i,j;

	interrupt(0x21,0,"i am inside sendMsg1",0,0);

	for(i=0;i<100;i++)
	{
		msgbuf[i]=*msg;
		msg++;
	}
        interrupt(0x21,0,"i am inside sendMsg2",0,0);

	Pmailbox = processNum*8*100;
	setKernelDataSegment();
	offset   = Pmailbox * CurrentProcess;
	restoreDataSegment();
	
	for(i=offset,j=0; j<100; i++,j++)
		putInMemory(0x9000,i,msgbuf[j]);

	for(i=0;i<8;i++)
	{
		setKernelDataSegment();
		if(msgAge[CurrentProcess][i]==processNum || msgAge[CurrentProcess][i] >0)
			(msgAge[CurrentProcess][i])++;
		restoreDataSegment();
	}
}
void getMessage (char *msg)
{
	int mailbox,i,oldestMsg=0,offset;
	char mailboxbuf[100*8],msgbuf[100];
	setKernelDataSegment();
	mailbox=CurrentProcess*8*100;
	restoreDataSegment();

	while(oldestMsg==0)
	{
	for(i=0;i<8;i++)
	{
		setKernelDataSegment();
		if(msgAge[CurrentProcess][i]>oldestMsg)
			oldestMsg=i;
		restoreDataSegment();
	}
	}
	
	offset = oldestMsg * mailbox;
//	for(i=offset,j=0; j<100; i++,j++)
  //              getFromMemory(0x9000,i,msgbuf[j]);
	printString(msgbuf);
	
	setKernelDataSegment();
        msgAge[CurrentProcess][oldestMsg]=0;
        restoreDataSegment();
	
	//print it just for testing

	/*for(i=0;i<8*100;i++)
	{
		if(mailboxbuf[i] ! =0)
		{	
			//loop get the message
			for(j=i,k=0;mailboxbuf[j] != '\0';j++,k++)
			{
				msg[k]=mailboxbuf[j];
				break;
			}
		}
		else
			i+=99;
	}*/
	
}
void handleInterrupt21(int AX, int BX, int CX, int DX)
{
	switch(AX)
	{
		case 0:
			printString(BX);
			break;
		case 1:
			readString(BX);
			break;
		case 2:
			readSector(BX, CX);
			break;
		case 3:
			//printString("List directory:");
			directory();
			break;
		case 4:
		    deleteFile(BX);
			break;
		case 5:
	            terminate();
			break;
		case 6:
			// BX is filename, CX is buffer pointer
			readFile(BX, CX);
			break;
		case 7:
			writeSector(BX, CX);
			break;
		case 8:
			writeFile(BX, CX);
			break;
		case 9:
			executeProgram(BX);
			break;
		case 10:
            // BX is int not char.
			killProcess(BX);
			break;
		 case 11:
                        sendMessage(BX, CX);
                        break;
		 case 12:
                        getMessage(BX, CX);
                        break;
		default:
			printString("Syscall not supported");
			break;
	}
}

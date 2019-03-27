//https://stackoverflow.com/questions/19322975/g-createprocess-no-such-file-or-directory
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <conio.h>
#include <unistd.h>
#include <time.h>

#include <windows.h>
#include "ni488.h"

#define arr_len(x) (sizeof(x) / sizeof(x[0]))

#define DebugCode (true)

static int GPIB = -1;                 // Board index

static int     PAD = -1;                      // Primary address
static int     SAD = 0;                      // Secondary address
static char  *CMDS = NULL;                //command to send 
static bool query  = false;              // is a query command 
static bool shutup = false;
static bool port   = false;
static bool debug  = false;             //debug switch
static const char *file_name = NULL;          //when file name specified, save binary response to file
static bool  overwrite = false;         //if file exist ,overwrite it

static int  skip_first_n_bytes = -1;    //for some system(DCA86100,AQ6370,etc.), transfered data via GPIB contain extra bytes,user can skip them
static char *strtoul_endptr = NULL;
static unsigned long  read_bytes = 0;   //read specified length of response then save to file
static bool  noibrdf = false;           //save file not use ibrdf() method, default will use idrdf() to save file 

typedef void (* f_on_receive)(const char *str, const int len);

struct gpib_dev
{
    int dev;
    f_on_receive on_receive;
};

struct instru_info 
{
    int pad;
    int sad;
    char idn[1024];
};

#define TIMEOUT               T10s  // Timeout value = 10 seconds
#define EOTMODE               1     // Enable the END message
#define EOSMODE               0     // Disable the EOS mode

char ErrorMnemonic[21][5] = {"EDVR", "ECIC", "ENOL", "EADR", "EARG",
                             "ESAC", "EABO", "ENEB", "EDMA", "",
                             "EOIP", "ECAP", "EFSO", "", "EBUS",
                             "ESTB", "ESRQ", "", "", "", "ETAB"};


void GPIBCleanup(int ud, const char* ErrorMsg);

#define dbg_print(...) if (!shutup) fprintf(stderr, __VA_ARGS__)

#define MAX_COMM_PACK_SIZE 65536

#define command_write_to_gpib       0
#define command_read_from_gpib      1
#define command_dbg_msg             2
#define command_shutdown            3

int read_exact(byte *buf, int len)
{
  int i, got=0;

  do 
  {
    if ((i = read(0, buf + got, len - got)) <= 0)
      return i;
    got += i;
  } while (got < len);

  return len;
}

int write_exact(byte *buf, int len)
{
  int i, wrote = 0;

  do 
  {
    if ((i = write(1, buf + wrote, len - wrote)) <= 0)
      return i;
    wrote += i;
  } while (wrote < len);

  return len;
}

int read_cmd(byte *buf)
{
  int len;

  if (read_exact(buf, 2) != 2)
    return -1;
  len = (buf[0] << 8) | buf[1];
  return read_exact(buf, len);
}

int write_cmd(byte *buf, int len)
{
  byte li;

  li = (len >> 8) & 0xff;
  write_exact(&li, 1);
  
  li = len & 0xff;
  write_exact(&li, 1);

  return write_exact(buf, len);
}

struct gpib_port_comm
{
    int len;
    char t;
    byte *b;
};

bool read_comm_cmd(gpib_port_comm &r)
{
    static byte cmd_buf[MAX_COMM_PACK_SIZE];
    r.len = -1;
    int len = read_cmd(cmd_buf);
    if (len < 0)
        return false;
    
    r.t = cmd_buf[0];
    r.len = len - 1;
    r.b = cmd_buf + 1;
    r.b[r.len] = 0;
    return true;
}

bool send_comm_response(const int t, const byte *s, const int len)
{
    static byte out_buf[MAX_COMM_PACK_SIZE];
    if (1 + len > MAX_COMM_PACK_SIZE)
        return false;

    out_buf[0] = t;
    memcpy(out_buf + 1, s, len);

    return write_cmd(out_buf, len + 1) > 0; 
}

void send_msg_response(const int t, const char *s)
{
    if (!shutup)
        send_comm_response(t, (const byte *)s, strlen(s));
}

void help(const char *args[])
{
    printf("GPIB client command options: \n");
    printf("    -port               as an Erlang port\n");
    printf("    -gpib   <N>         board index(GPIB board index)\n");
    printf("    -pad    <N>         primary address\n");
    printf("    -sad    <N>         secondary address\n");
    printf("    -ls                 list all instruments on a board and quit\n");
    printf("    -shutup             suppress all error/debug prints\n");    
    printf("    -debug              prints debug messages\n"); 	
    printf("    -cmdstr <strings>   commands to send to the device\n");  
    printf("    -query              the command is a query command \n");        
    printf("    -save2file          save the response binary data to specify file");
    printf("         -skip          skip first n bytes of received file\n");
    printf("         -noibrdf       save file not use ibrdf() method, default will use idrdf() to save file\n");
	 printf("         -rBytes        if -noibrdf specified ,must specify how many bytes should be read, but what should be noticed is that the : ibcntl : always store the actually transfer byte of length\n");
	printf("         -overwrite     if file exist ,overwrite it\n");	
    printf("    -help/-?            show this information\n\n");
    printf("Typical usage (Agilent 34401A on GPIB board index 0  with primary address 22 and secondary address 0 ) is\n\n");
    printf("    Just send Command:\n");
    printf("                 %s  -gpib 0 -pad 22 -cmdstr \"CONFigure:CONTinuity\" \n",args[0]);
    printf("    or send Command then read response immediately: \n");
    printf("                 %s  -gpib 0 -pad 22 -cmdstr \"READ?\" -query \n",args[0]);
    printf("    or combine format \n");
    printf("                 %s  -gpib 0 -pad 22  -query -cmdstr \"CONFigure:CONTinuity ; READ?\" \n",args[0]);
    printf("    or communicate with device Interactively:\n");
    printf("                 %s  -gpib 0 -pad 22\n",args[0]);
    printf("    http://mikrosys.prz.edu.pl/KeySight/34410A_Quick_Reference.pdf \n\n");
    printf("    http://ecee.colorado.edu/~mathys/ecen1400/pdf/references/HP34401A_BenchtopMultimeter.pdf \n\n");
    printf("Typical usage to save file(Agilent DCA86100 Legacy UI on GPIB board index 0  with primary address 7 and secondary address 0 ) is\n\n");
    printf("    Please refer to :https://www.keysight.com/upload/cmc_upload/All/86100_Programming_Guide.pdf#page=176\n");    
	printf("                 %s  -gpib 0 -pad 7  -query -cmdstr \":DISPlay:DATA? JPG\" -save2file \"DCA86100 Legacy UI Screen Capture.jpg\" -skip 7 \n",args[0]);
    printf("!Note: if -cmdstr not specified ,Press Enter (empty input) to read device response\n");
    //usleep(2000);
}

int list_instruments() // list all instruments 
{

    int        Num_Instruments;            // Number of instruments on GPIB
    Addr4882_t Instruments[31];            // Array of primary addresses
    Addr4882_t Result[31];                 // Array of listen addresses
    instru_info infos[31];
    int loop;

    SendIFC(GPIB);
    if (ibsta & ERR)
    {
       GPIBCleanup(GPIB, "Unable to open board");
       return 1;
    }

    for (loop = 0; loop < 30; loop++) {
       Instruments[loop] = (Addr4882_t)(loop + 1);
    }
    Instruments[arr_len(Instruments) - 1] = NOADDR;

    printf("Finding all instruments on the bus...\n\n");
    FindLstn(GPIB, Instruments, Result, arr_len(Instruments));
    if (ibsta & ERR)
    {
       GPIBCleanup(GPIB, "Unable to issue FindLstn call");
       return 1;
    }

    Num_Instruments = ibcntl;
    printf("Number of instruments found = %d\n\n", Num_Instruments);
    Result[Num_Instruments] = NOADDR;

    for (loop = 0; loop < Num_Instruments; loop++)
    {
       infos[loop].pad = GetPAD(Result[loop]);
       infos[loop].sad = GetSAD(Result[loop]);
    }
    ibonl(GPIB, 0);

    for (loop = 0; loop < Num_Instruments; loop++)
    {
        int Dev = ibdev(GPIB, infos[loop].pad, infos[loop].sad,
              TIMEOUT, EOTMODE, EOSMODE);
        if (ibsta & ERR)
        {
           GPIBCleanup(Dev, "Unable to open device");
           return 1;
        }

        ibclr(Dev);
        if (ibsta & ERR)
        {
           GPIBCleanup(Dev, "Unable to clear device");
           return 1;
        }

        ibwrt(Dev, (void *)"*IDN?", 5L);
        if (ibsta & ERR)
        {
           GPIBCleanup(Dev, "Unable to write to device");
           return 1;
        }

        ibrd(Dev, infos[loop].idn, arr_len(infos[loop].idn) - 1);
        if (ibsta & ERR)
        {
           GPIBCleanup(Dev, "Unable to read data from device");
           return 1;
        }
        infos[loop].idn[ibcntl] = '\0';
        ibonl(Dev, 0);
    }

    for (loop = 0; loop < Num_Instruments; loop++)
    {
        if (infos[loop].sad == NO_SAD)
        {
            printf("The instrument at %d is %s(PAD = %d, SAD = NONE)\n",
                   loop + 1, infos[loop].idn, infos[loop].pad);
        }
        else
        {
            printf("The instrument at %d is %s(PAD = %d, SAD = %d)\n",
                   loop + 1, infos[loop].idn, infos[loop].pad, infos[loop].sad);
        }
    }    

    return 0;
}

void gpib_shutdown(gpib_dev *dev)
{
    //ibnotify(dev->dev, 0, NULL, NULL);
    ibonl(dev->dev, 0);
    dev->dev = -1;
}

/*
 *  After each GPIB call, the application checks whether the call
 *  succeeded. If an NI-488.2 call fails, the GPIB driver sets the
 *  corresponding bit in the global status variable. If the call
 *  failed, this procedure prints an error message, takes the board
 *  offline and exits.
 */
void GPIBCleanup(int ud, const char* ErrorMsg)
{
    dbg_print("Error : %s\nibsta = 0x%x iberr = %d (%s)\n",
               ErrorMsg, ibsta, iberr, ErrorMnemonic[iberr]);
    dbg_print("Cleanup: Taking board offline\n");
    ibnotify(ud, 0, NULL, NULL);    
    ibonl(ud, 0);
}

static gpib_dev dev;

BOOL ctrl_handler(DWORD fdwCtrlType) 
{ 
    switch (fdwCtrlType) 
    {
        case CTRL_C_EVENT: 
            gpib_shutdown(&dev);
            exit(0);
            return TRUE;
  
        case CTRL_BREAK_EVENT: 
            return FALSE; 

        // console close/logoff/shutdown
        case CTRL_CLOSE_EVENT: 
        case CTRL_LOGOFF_EVENT: 
        case CTRL_SHUTDOWN_EVENT:
            gpib_shutdown(&dev);
            exit(0);
            return FALSE; 

        default: 
            return FALSE; 
    } 
}

int as_port(gpib_dev *dev);
int interactive(gpib_dev *dev);
int operate_once(gpib_dev *dev);
int remove_first_n_bytes_from_file(const char *file_name, int n_bytes);
int read_n_bytes_use_ibrd_and_save_to_file(gpib_dev *dev,const char *file_name, unsigned long n_bytes);
const char *get_filename_ext_without_dot(const char *filename);
const char *generate_new_file_name(const char* old_file_name,char * new_file_name);


int __stdcall cb_on_rqs(int LocalUd, int LocalIbsta, int LocalIberr, 
      long LocalIbcntl, void *RefData);
void stdout_on_receive(const char *s, const int len);
void port_on_receive(const char *s, const int len);

int main(const int argc, const char *args[])
{   
    dev.dev = -1;

#define load_i_param(var, param) \
    if (strcmp(args[i], "-"#param) == 0)   \
    {   var = atoi(args[i + 1]); i += 2; }

#define load_s_param(var, param) \
    if (strcmp(args[i], "-"#param) == 0)   \
    {   var = (char *)args[i + 1]; i += 2; }   //{   strcpy(var, args[i + 1]); i += 2; } is wrong

#define load_b_param(param) \
    if (strcmp(args[i], "-"#param) == 0)   \
    {   param = true; i++; }    
//https://stackoverflow.com/questions/27260304/equivalent-of-atoi-for-unsigned-integers/27278489#27278489
//
#define load_ul_param(var, param) \
	if (strcmp(args[i], "-"#param) == 0)   \
	{	var = strtoul(args[i + 1],&strtoul_endptr,10); i += 2; }



 
    if (1 == argc ) //if no paraments specified,show help
        {
            help(args);
            return -1;
        }
        
    int i = 1;
    while (i < argc) //prase paraments
    {
		#if DebugCode
		printf("args[%d]=%s\n",i,args[i]); 
		#endif
        load_i_param(GPIB, gpib)
        else load_i_param(SAD, sad)
        else load_i_param(PAD, pad)

        else load_b_param(shutup)
        else load_b_param(port)
        else load_s_param(CMDS, cmdstr)
        else load_b_param(query)
		else load_b_param(debug)
		else load_s_param(file_name, save2file)
		else load_b_param(overwrite)
		else load_i_param(skip_first_n_bytes, skip)
		else load_b_param(noibrdf)
		else load_ul_param(read_bytes,rBytes)
        else if (strcmp(args[i], "-ls") == 0) 
        {
            return list_instruments();
        }
        else if ((strcmp(args[i], "-help") == 0) || (strcmp(args[i], "-?") == 0)|| (strcmp(args[i], "/?") == 0))
        {
            help(args);
            return -1;
        }
        else
            i++;
    }
    if (GPIB < 0 || PAD < 0 ) 
    {
        printf("GPIB board index , primary address must be specified: %s  -gpib 0 -pad 22 \n",args[0]);
        printf("number of arguments :%d\n",argc);
        return -1;  // address must be specified
    }

    if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)ctrl_handler, TRUE))
        dbg_print("WARNING: SetConsoleCtrlHandler failed.\n");

    ibconfig(GPIB, IbcAUTOPOLL, 1);

    dev.dev = ibdev(GPIB, PAD, SAD, TIMEOUT, EOTMODE, EOSMODE);
    if (ibsta & ERR)
    {
       GPIBCleanup(dev.dev, "Unable to open device");
       return 1;
    }    

    ibclr(dev.dev);
    if (ibsta & ERR)
    {
       GPIBCleanup(dev.dev, "Unable to clear device");
       return 1;
    }

    dev.on_receive = stdout_on_receive;
    if (port)
        dev.on_receive = port_on_receive;

    // set up the asynchronous event notification on RQS
    // ibnotify(dev.dev, RQS | TIMO, cb_on_rqs, &dev);
    // if (ibsta & ERR)  
    // {
    //     GPIBCleanup(dev.dev, "ibnotify call failed.\n");
    //     return 0;
    // }
    
    if (debug) printf("CMDS string length =%d\n",strlen(CMDS)+1);   
    if (CMDS) //if CMDS NOT empty ,operate once, write or query
     {
        if (debug) { printf("CMDS:   %s \n",CMDS); }
        return  operate_once(&dev);
         
     }
    if (port)
    {
        setmode(0, O_BINARY);
        setmode(1, O_BINARY);
        return as_port(&dev);
    }
    else
    {
        if (!shutup)
            printf("Tip: Press Enter to read response\n");
        return interactive(&dev);
    }
}

int port_read(gpib_dev *dev)
{
    char s[3240 + 1];
    ibrd(dev->dev, s, sizeof(s) - 1); 
    send_msg_response(command_dbg_msg, "ibrd");
    if (ibsta & ERR)
    {
        if (iberr == EABO)
            return 0;
        GPIBCleanup(dev->dev, "Unable to read data from device");
        return 1;
    }
    send_msg_response(command_dbg_msg, "send_ ing");
    send_comm_response(command_read_from_gpib, (byte *)s, ibcntl);
    return 0;
}

int as_port(gpib_dev *dev)
{
    char s[3240 + 1]; send_msg_response(command_dbg_msg, "as_port");
    while (true)
    {
        gpib_port_comm c;
        send_msg_response(command_dbg_msg, "wait for command");
        read_comm_cmd(c);
        send_msg_response(command_dbg_msg, "read_comm_cmd");
        switch (c.t)
        {
        case command_write_to_gpib:
            send_msg_response(command_dbg_msg, "command_write_to_gpib");
            if (c.len < 1) 
                continue;

            ibwrt(dev->dev, c.b, c.len);
            if (ibsta & ERR)
            {
               GPIBCleanup(dev->dev, "Unable to write to device");
               return 1;
            }

            break;
        case command_read_from_gpib:
            send_msg_response(command_dbg_msg, "command_read_from_gpib");

            if (port_read(dev) != 0)
                return 1;
            break;
        default:
            gpib_shutdown(dev);
            return 0;
        }

        
    }
}

int interactive(gpib_dev *dev)
{
    while (true)
    {
        char s[10240 + 1];

        s[0] = '\0';
        gets(s);
        if (strlen(s) >= sizeof(s) - 1)
        {
            gpib_shutdown(dev);
            break;
        }

        if (strlen(s) > 0)
        {
            ibwrt(dev->dev, s, strlen(s));
            if (ibsta & ERR)
            {
               GPIBCleanup(dev->dev, "Unable to write to device");
               return 1;
            }
        }
        else    // strlen(s) = 0, read response
        {
            ibrd(dev->dev, s, sizeof(s) - 1);
            if (ibsta & ERR)
            {
                if (iberr == EABO) continue;
                GPIBCleanup(dev->dev, "Unable to read data from device");
                return 1;
            }
            s[ibcntl] = '\n'; s[ibcntl + 1] = '\0';
            printf(s);
        }
    }
    return 0; //GPIB.c:517:1: warning: control reaches end of non-void function [-Wreturn-type] https://www.ibm.com/support/knowledgecenter/en/SSB23S_1.1.0.15/common/m1rhnvf.html
}

char CLS[] ="*CLS";
int operate_once(gpib_dev *dev) // write or query , only once
{
        char s[1024 + 1]; // read buffer , for common use 1024 bytes is enough
        s[0] = '\0';      // fill first character as  string terminator "\0"
             // clear device first
            ibwrt(dev->dev,CLS, strlen(CLS));
            // write CMDS to device
            ibwrt(dev->dev, CMDS, strlen(CMDS));
            if (ibsta & ERR)
            {
               GPIBCleanup(dev->dev, "Unable to write to device");
               return 1;
            }
           //Read response from device 
       if(query) //
       {
			if (file_name)                 //if file name specified,read data bytes to file // https://linux-gpib.sourceforge.io/doc_html/reference.html
			{
				       if (debug) printf("file_name is : %s \n file name string length = %d\n",file_name,strlen(file_name)+1); 
					   if(!overwrite && access( file_name, F_OK ) != -1 ) //if file exist && not overwrite, save a new file name //https://stackoverflow.com/questions/230062/whats-the-best-way-to-check-if-a-file-exists-in-c/230068#230068
					   	{	
					   	 char  new_file_name[300+1];
						 file_name = generate_new_file_name(file_name,new_file_name); //update file name
						if (debug) printf("File exist,new file_name is : %s \n file name string length = %d\n",file_name,strlen(file_name)+1); 
					   	}
                       if(noibrdf) //use idrd()  read  specified length of response then save it to file
					   	{
					   	  if(read_bytes > 0) //
						  	{
						  			read_n_bytes_use_ibrd_and_save_to_file(dev,file_name,read_bytes);
									if (debug) printf("read_n_bytes_use_ibrd_and_save_to_file(%d,%s,%lu)\n",dev->dev,file_name,read_bytes);
					   	  	}
						  else      //-rBytes not specified 
						  	{
							   printf("Hence how many bytes need to be transfer not specified,no data transfered,please specified the -rBytes\n");
					   	  	}
						  	
                       	}
						else //use ibrdf() read response then save it to file
						{ 
						 if (debug) printf("ibrdf(dev->dev,file_name) used to save file: %s\n",file_name,read_bytes);
				        ibrdf(dev->dev,file_name);    // tail -c+9 1.jpg >2.JPG  https://stackoverflow.com/questions/4411014/how-to-get-only-the-first-ten-bytes-of-a-binary-file/4411216#4411216
                          if (ibsta & ERR)
                            {
                            GPIBCleanup(dev->dev, "Unable to read data from device\n");
                            return 1;
                            }
						}
				if (debug) printf("actually %ld bytes data transfered\n",ibcntl);
				if(0 < skip_first_n_bytes) {remove_first_n_bytes_from_file(file_name, skip_first_n_bytes);} //if user wants to remove first n bytes,remove them
				goto EndOfOperateOnce;
	       }
			else
		   {
            //printf("query:   %d\n",query);  
            ibrd(dev->dev, s, sizeof(s) - 1);
            if (ibsta & ERR)
            {
                GPIBCleanup(dev->dev, "Unable to read data from device");
                return 1;
            }
            s[ibcntl] = '\0';
            printf(s);
		   }
       }
EndOfOperateOnce:
	   gpib_shutdown(dev); //shutdown gpib at last
       return 0;

}

void stdout_on_receive(const char *s, const int len)
{
    printf(s);
}

void port_on_receive(const char *s, const int len)
{
    send_comm_response(command_read_from_gpib, (const byte *)s, len);
}

int __stdcall cb_on_rqs(int LocalUd, int LocalIbsta, int LocalIberr, 
      long LocalIbcntl, void *RefData)
{
    gpib_dev *dev = (gpib_dev *)(RefData);

#define fatal_error(s)  \
    {                   \
        GPIBCleanup(dev->dev, s); \
        exit(-1);                   \
    } while (false)   

#define expectedResponse 0x43

   char SpollByte;
   char ReadBuffer[4000];

   // If the ERR bit is set in LocalIbsta, then print an error message 
   // and return.
   if (LocalIbsta & ERR)  {
      fatal_error("GPIB error has occurred. No more callbacks.");
   }
   
   // Read the serial poll byte from the device. If the ERR bit is set
   // in ibsta, then print an error message and return.
   LocalIbsta = ibrsp(LocalUd, &SpollByte);
   if (LocalIbsta & ERR)  {
      fatal_error("GPIB error has occurred. No more callbacks.");
   }

   // If the returned status byte equals the expected response, then 
   // the device has valid data to send; otherwise it has a fault 
   // condition to report.
   //if (SpollByte != expectedResponse)   {
   //   fatal_error("GPIB error has occurred. No more callbacks.");
   // }

   // Read the data from the device. If the ERR bit is set in ibsta, 
   // then print an error message and return.
   LocalIbsta = ibrd(LocalUd, ReadBuffer, sizeof(ReadBuffer) - 1);
   if (LocalIbsta & ERR)  {
      fatal_error("GPIB error has occurred. No more callbacks.");
   }

   ReadBuffer[ibcntl] = '\0';
   dev->on_receive(ReadBuffer, ibcntl);

   return RQS;
}

//https://stackoverflow.com/questions/7749134/reading-and-writing-a-buffer-in-binary-file
//https://www.linuxquestions.org/questions/programming-9/c-howto-read-binary-file-into-buffer-172985/
int remove_first_n_bytes_from_file(const char *file_name, int n_bytes)
{
FILE            *file = NULL;
char          *buffer = NULL;
unsigned long fileLen = 0;

//Open file
file = fopen(file_name, "rb");
if (!file)
{
	fprintf(stderr, "Unable to open file %s\n", file_name);
	return -1 ;
}

//Get file length
fseek(file, 0, SEEK_END);
fileLen=ftell(file);
fseek(file, 0, SEEK_SET);

//Allocate memory
buffer=(char *)malloc(fileLen+1);
if (!buffer)
{
	fprintf(stderr, "Memory error!\n");
	fclose(file);
	return -2;
}

//Read file contents into buffer
fread(buffer, fileLen, 1, file);
fclose(file);

//Do what ever with buffer       //
/* Write your buffer to disk. */
file = fopen(file_name, "wb+");

if (file){
	//buffer = buffer + n_bytes;				//delete first n bytes, thus buffer pointer move right  n_bytes
    fwrite(buffer + n_bytes, fileLen - n_bytes, 1, file);
   if(debug) printf("first %d bytes of %s has benn removed\n",n_bytes,file_name);
}
else{
    puts("Something wrong writing to File.\n");
}

fclose(file);
//buffer -= n_bytes;                        //reset buffer pointer,if not free memory cause segment fault
free(buffer);
return EXIT_SUCCESS;
}

int read_n_bytes_use_ibrd_and_save_to_file(gpib_dev *dev,const char *file_name, unsigned long n_bytes)
{
FILE          *file = NULL;
char          *buffer = NULL;
buffer = (char *)malloc(n_bytes+1);// memory allocate
if (!buffer)
{
	fprintf(stderr, "Memory error!\n");
	return -2;
}

ibrd(dev->dev, buffer , n_bytes);

if (ibsta & ERR)                   // Error occur,Clean up device and free memory
  {
  GPIBCleanup(dev->dev, "Unable to read data from device\n");
  free(buffer);
  return -1;
  }

/* Write buffer to disk. */
file = fopen(file_name, "wb+");
if (file){
    fwrite(buffer,(ibcntl < n_bytes ? ibcntl : n_bytes), 1, file);  //no matter how big -rBytes specified, only write ibcntl bytes to file, it's meaningless to write extra null bytes to the file which only increase the file size
}
else{
    puts("Something wrong writing to File.\n");
}
fclose(file);
free(buffer); //free memory
return EXIT_SUCCESS;
}

//https://stackoverflow.com/questions/5309471/getting-file-extension-in-c/5309508#5309508
const char *get_filename_ext_without_dot(const char *filename) 
{
    const char *dot = strrchr(filename, '.'); //find the posistion of .  possible situation  : nameext  .nameext  name.ext. ...  name.ext .name.ext 
    if(!dot || dot == filename)  //no extension:  nameext /or no file name: .nameext
    {
		return filename + strlen(filename); //return the pointer to '\0'(end of string)
    }
	else
    {
           return dot + 1;           //name.ext. ... name.ext  .name.ext  just return the pointer after .
    }
    
}
const char *generate_new_file_name(const char* old_file_name,char * new_file_name)
{
   char new_file_name_full[300 + 1];
   static unsigned int  file_name_length = 0;
   const char *new_file_extension = NULL;
   new_file_name_full[0] ='\0';

  //https://stackoverflow.com/questions/3673226/how-to-print-time-in-format-2009-08-10-181754-811/3673291#3673291
  //get current time 
  time_t rawtime;
  char current_time[30];
  struct tm* tm_info;
  time(&rawtime);
  tm_info = localtime(&rawtime);
  strftime(current_time, sizeof(current_time), "%Y-%m-%d %H:%M:%S", tm_info);

  //get file name and file extension
  new_file_extension = get_filename_ext_without_dot(old_file_name);

  if(!(*new_file_extension)) // if value of *ptr is '\0' , prove that no extension
      file_name_length = strlen(old_file_name);// sizeof(old_file_name) will always return 4
  else  //extension exist , file name length is  new_file_extension-old_file_name-1 //substrate two pointer  return the length of the string  
      file_name_length = new_file_extension-old_file_name-1;


   //generate new file name 
   //https://stackoverflow.com/questions/1000556/what-does-the-s-format-specifier-mean/1000574#1000574
   //https://stackoverflow.com/questions/5932214/printf-string-variable-length-item/5932385#5932385
   snprintf(new_file_name_full, sizeof(new_file_name_full), "%.*s_%s%s",file_name_length,old_file_name,current_time,new_file_extension);// oldname_time.ext
   snprintf(new_file_name,sizeof(new_file_name),"%s",new_file_name_full);
   return (const char *) new_file_name;
}

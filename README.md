KissGPIB
========

A GPIB client based on KISS principle.

Input an string and press Enter to write it to GPIB device. 
Press Enter directly (an empty input) to read device's response.  

Use -? to get help on command line options.

There are two implementations.

#### Classic

GPIB.c uses NI's classic APIs, ibrd, ibwrt, etc. GCC can be used to build this.

```
GPIB client command options:
    -port               as an Erlang port
    -gpib   <N>         board index(GPIB board index)
    -pad    <N>         primary address
    -sad    <N>         secondary address
    -ls                 list all instruments on a board and quit
    -shutup             suppress all error/debug prints
    -cmdstr <strings>   commands to send to the device
    -query              the command is a query command
    -save2file          save the response binary data to specify file
         -skip          skip first n bytes of received file
    -help/-?            show this information

Typical usage (Agilent 34401A on GPIB board index 0  with primary address 22 and secondary address 0 ) is

    Just send Command:
                 GPIB.exe  -gpib 0 -pad 22 -cmdstr "CONFigure:CONTinuity"
    or send Command then read response immediately:
                 GPIB.exe  -gpib 0 -pad 22 -cmdstr "READ?" -query
    or combine format
                 GPIB.exe  -gpib 0 -pad 22  -query -cmdstr "CONFigure:CONTinuity ; READ?"
    or communicate with device Interactively:
                 GPIB.exe  -gpib 0 -pad 22
    http://mikrosys.prz.edu.pl/KeySight/34410A_Quick_Reference.pdf

    http://ecee.colorado.edu/~mathys/ecen1400/pdf/references/HP34401A_BenchtopMultimeter.pdf
Typical usage to save file(Agilent DCA86100 Legacy UI on GPIB board index 0  with primary address 7 and secondary address 0 ) is

Please refer to :https://www.keysight.com/upload/cmc_upload/All/86100_Programming_Guide.pdf#page=176
                 GPIB.exe  -gpib 0 -pad 7  -query -cmdstr ":DISPlay:DATA? JPG" -save2file "DCA86100 Legacy UI Screen Capture.jpg" -skip 7
!Note: if -cmdstr not specified ,Press Enter (empty input) to read device response

```

#### VISA

GPIB.c uses VISA APIs, viRead, viWrite, etc. GCC can't be used to build this, while VC is OK.

This version supports both GPIB and LAN-GPIB (VXI 11.3).

```
 GPIB client command options:
     -port               as an Erlang port
     -board  <N>         (LAN) board index
     -ip     'IP addr'   (LAN) IP address string
     -name   <Name>      (LAN) device name
     -gpib   <N>         (GPIB) board handle
     -pad    <N>         (GPIB) primary address
     -sad    <N>         (GPIB) secondery address
     -ls                 list all instruments on a board and quit
     -shutup             suppress all error/debug prints
     -help/-?            show this information
```

NOTE: 
* ./ni: Copyright 2018 National Instruments Corporation
* ./visa: Distributed by IVI Foundation Inc., Contains National Instruments extensions. 

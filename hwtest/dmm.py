#   Just a quick n' dirty script to read the Data from a VA18B multimeter.
#   This DMM uses the low nibble of 14 bytes to send the lcd segments.
#
#       License: GPL v3. http://www.gnu.org/licenses/gpl-3.0-standalone.html
#
# source: https://alexkaltsas.wordpress.com/2013/04/19/python-script-to-read-data-from-va18b-multimeter/
 
import signal
import sys
import time
import serial
import io
import getopt
 
interval = '1.0'
device = '/dev/ttyUSB0'
 
try:
    port=serial.Serial(port=device,
               baudrate=2400,
               bytesize=serial.EIGHTBITS,
               stopbits=serial.STOPBITS_ONE,
               parity=serial.PARITY_NONE,
               timeout=None)
     
    if not port.isOpen():
        port.open()
 
except IOError,err:
    print '\nError:' + str(err) + '\n'
    sys.exit(1)
 
# Every packet is 14 bytes long.
def get_bytes():
    i=0
    substr=''
    while i<=13:
        byte=port.read(1)
        # converting every byte to binary format keeping the low nibble.
        substr += '{0:08b}'.format(ord(byte))[4:]
        i += 1
    return substr
       
def stream_decode(substr):
   
    ac       = int(substr[0:1])
    dc       = int(substr[1:2])
    auto     = int(substr[2:3])
    pclink   = substr[3:4]
    minus    = int(substr[4:5])
     
    digit1   = substr[5:12]
    dot1     = int(substr[12:13])
    digit2   = substr[13:20]
    dot2     = int(substr[20:21])
    digit3   = substr[21:28]
    dot3     = int(substr[28:29])
    digit4   = substr[29:36]
     
    micro    = int(substr[36:37])
    nano     = int(substr[37:38])
    kilo     = int(substr[38:39])
    diotst   = int(substr[39:40])
    mili     = int(substr[40:41])
    percent  = int(substr[41:42])
    mega     = int(substr[42:43])
    contst   = int(substr[43:44])
    cap      = int(substr[44:45])
    ohm      = int(substr[45:46])
    rel      = int(substr[46:47])
    hold     = int(substr[47:48])
    amp      = int(substr[48:49])
    volts    = int(substr[49:50])
    hertz    = int(substr[50:51])
    lowbat   = int(substr[51:52])
    minm     = int(substr[52:53])
    fahrenh  = substr[53:54]
    celcius  = int(substr[54:55])
    maxm     = int(substr[55:56])
     
    digit = {"1111101":"0",
             "0000101":"1",
             "1011011":"2",
             "0011111":"3",
             "0100111":"4",
             "0111110":"5",
             "1111110":"6",
             "0010101":"7",
             "1111111":"8",
             "0111111":"9",
             "0000000":"",
             "1101000":"L"}
     
    value = ("-" if minus else " ") +\
            digit.get(digit1,"") + ("." if dot1 else "") +\
            digit.get(digit2,"") + ("." if dot2 else "") +\
            digit.get(digit3,"") + ("." if dot3 else "") +\
            digit.get(digit4,"")
 
    flags = " ".join(["AC"         if ac     else "",
                      "DC"         if dc     else "",
                      "Auto"       if auto   else "",
                      "Diode test" if diotst else "",
                      "Conti test" if contst else "",
                      "Capacity"   if cap    else "",
                      "Rel"        if rel    else "",
                      "Hold"       if hold   else "",
                      "Min"        if minm   else "",
                      "Max"        if maxm   else "",
                      "LowBat"     if lowbat else ""])
                      
    units = ("n"    if nano    else "") +\
            ("u"    if micro   else "") +\
            ("k"    if kilo    else "") +\
            ("m"    if mili    else "") +\
            ("M"    if mega    else "") +\
            ("%"    if percent else "") +\
            ("Ohm"  if ohm     else "") +\
            ("Amp"  if amp     else "") +\
            ("Volt" if volts   else "") +\
            ("Hz"   if hertz   else "") +\
            ("C"    if celcius else "")
            
    return value
     
def get_voltage():
    substr = get_bytes()
    data = stream_decode(substr)
    port.flushInput()
    return data

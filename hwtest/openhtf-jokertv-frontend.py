# Copyright 2018 Joker Systems Inc. All Rights Reserved.
# Abylay Ospan <aospan@jokersys.com>
# https://jokersys.com/joker-tv
# GPLv2

"""Joker TV OpenHTF test with Web GUI"""

import os
import time
import telnetlib
import importlib
import subprocess

import openhtf as htf
from openhtf.util import conf

from openhtf.output.servers import station_server
from openhtf.output.web_gui import web_launcher
from openhtf.plugs import user_input

pgm_path = "/mnt/sdd/altera/17.1/quartus/bin/quartus_pgm"
joker_tv_path = "../build/joker-tv"
tscheck_path = "../build/tscheck"

dmm = importlib.import_module("dmm")

# FPGA firmware
@htf.measures(htf.Measurement('jokertv_fpga_measurement'))
def jokertv_fpga(test):
  test.logger.info('Write FPGA firmware')
  # write fw to flash
  ret = os.system('%s -c 1 -i joker-tv.cdf' % (pgm_path))
  ret = 0
  if ret != 0:
      return
  # reconfigure FPGA on the fly
  ret = os.system('%s -c 1 -z --mode=JTAG  --operation="p;../fw/fw-0.37/joker_tv-0.37.sof"' % (pgm_path))
  if ret != 0:
      return
  time.sleep(2)
  test.measurements.jokertv_fpga_measurement = 'Done'

# CAM module test
@htf.measures(htf.Measurement('jokertv_cam_measurement'))
def jokertv_cam(test):
  test.logger.info('test CAM')
  ## start joker-tv 
  ret = os.system('%s -g -c -t &' % (joker_tv_path))
  if ret != 0:
      return
  time.sleep(5)

  ## check CAM presence and MMI
  ## specific for DRE-Crypt module (Neotion)
  ## change for your module accordingly
  timeout = 4
  try:
    tn = telnetlib.Telnet('127.0.0.1', 7777, timeout)
    test.logger.info('CAM: connected')
    tn.read_until("2: Settings", timeout)
    test.logger.info('CAM: MMI read ok')
    tn.write("2")
    tn.read_until("4: Factory reset", timeout)
    test.logger.info('CAM: MMI write ok')
  except:
    os.system('killall joker-tv')
    raise

  ## tscheck. collect some TS
  test.logger.info('CAM: tscheck')
  time.sleep(5)
  ret = os.system('%s -f out.ts -p' % (tscheck_path))
  if ret != 0:
    os.system('killall joker-tv')
    return

  ret = os.system('killall joker-tv')
  test.measurements.jokertv_cam_measurement = 'Done'

# Satellite test
@htf.measures(htf.Measurement('jokertv_sat_measurement'))
def jokertv_sat(test):
  test.logger.info('test Sat (DVB-S/S2)')

  ## start joker-tv 
  ret = os.system('%s -d 6 -f 11800000000 -s 20000000 -y 18 -p -z 10600,10600,11600 &' % (joker_tv_path))
  if ret != 0:
      return
  time.sleep(4)

  volt = 0.0
  ## Check voltage on Sat output (LNB power)
  try:
    ### ignore first data from multimeter
    dmm.get_voltage()
    volt = dmm.get_voltage()
    test.logger.info('voltage=%s' % volt)
    volt = float(volt)
    test.logger.info('float voltage=%f' % volt)
  except:
    test.logger.info('Sat: cant read voltage')
    os.system('killall joker-tv')
    raise

  if volt < 17.5:
    test.logger.info('Sat: voltage not in range !')
    os.system('killall joker-tv')
    raise

  ## tscheck. collect some TS
  test.logger.info('Sat: tscheck')
  time.sleep(5)
  ret = os.system('%s -f out.ts -p' % (tscheck_path))
  if ret != 0:
    os.system('killall joker-tv')
    return

  ret = os.system('killall joker-tv')
  test.measurements.jokertv_sat_measurement = 'Done'

# Terrestrial test
@htf.measures(htf.Measurement('jokertv_ter_measurement'))
def jokertv_ter(test):
  test.logger.info('test Terrestrial (DVB-T)')

  ## Change values according your DVB-T modulator
  ## start joker-tv 
  ret = os.system('%s -d 3 -f 150000000 -b 6000000 &' % (joker_tv_path))
  if ret != 0:
      return

  ## collect some TS and check file size
  ## something wrong if file size less than 24MB
  time.sleep(10)
  size = os.stat("out.ts").st_size
  test.logger.info('Ter: TS file size %s' % size)
  if size < (20.0 * 1024 * 1024):
    test.logger.info('Ter: TS file size too small')
    os.system('killall joker-tv')
    return

  ret = os.system('killall joker-tv')
  test.measurements.jokertv_ter_measurement = 'Done'

# DTMB chip detection test (no TS stream tests yet)
@htf.measures(htf.Measurement('jokertv_dtmb_measurement'))
def jokertv_dtmb(test):
  test.logger.info('test DTMB chip presence')
  ## DTMB chip test
  cmd = [joker_tv_path, '-d 13 -f 150000000']
  p = subprocess.Popen(cmd, stdout=subprocess.PIPE,
          stderr=subprocess.PIPE,
          stdin=subprocess.PIPE)
  time.sleep(5)
  os.system('killall joker-tv')
  out, err = p.communicate()
  counter = 0
  for line in out.splitlines():
      if line.startswith('INFO: status=11 (NOLOCK) ucblocks=0, rflevel'):
          counter += 1
  test.logger.info('DTMB: counter %d' % counter)
  if counter < 5:
    test.logger.info('DTMB: counter out of range. DTMB chip error !')
    raise
  test.measurements.jokertv_dtmb_measurement = 'Done'

# ATSC chip detection test (no TS stream tests yet)
@htf.measures(htf.Measurement('jokertv_atsc_measurement'))
def jokertv_atsc(test):
  test.logger.info('test ATSC chip presence')
  ## ATSC chip test
  cmd = [joker_tv_path, '-d 11 -f 150000000']
  p = subprocess.Popen(cmd, stdout=subprocess.PIPE,
          stderr=subprocess.PIPE,
          stdin=subprocess.PIPE)
  time.sleep(5)
  os.system('killall joker-tv')
  out, err = p.communicate()
  counter = 0
  for line in out.splitlines():
      if line.startswith('INFO: status=11 (NOLOCK) ucblocks=255'):
          counter += 1
  test.logger.info('ATSC: counter %d' % counter)
  if counter < 5:
    test.logger.info('ATSC: counter out of range. ATSC chip error !')
    raise
  test.measurements.jokertv_atsc_measurement = 'Done'


if __name__ == '__main__':
  conf.load(station_server_port='4444')
  with station_server.StationServer() as server:
    web_launcher.launch('http://localhost:4444')
    while 1:
      test = htf.Test(jokertv_fpga, jokertv_cam, jokertv_sat, jokertv_ter, jokertv_dtmb, jokertv_atsc)
      test.add_output_callbacks(server.publish_final_state)
      test.execute(test_start=user_input.prompt_for_test_start())

#!/usr/bin/python

import xmltodict

freqs_list = []
fin = open("dvb_t2_oirt_freq.xml")
xml = fin.read()
res = xmltodict.parse(xml, process_namespaces=True)
doc = res['document'];
for row in range(0, len(doc['row'])):
    freq = doc['row'][row]['frequency_mhz']
    bw = doc['row'][row]['bandwidth']
    mod = doc['row'][row]['modulation']
    freqs_list.append({'@frequency_mhz': freq, '@bandwidth': bw,
        '@modulation': mod})

standard = doc['row'][0]['delivery_system']
freqs = {'@standard': standard, 'freq': freqs_list}
standards = {'delivery_system': freqs }
root = {'document': standards}
# print dict to xml
print """<!-- Frequencies for TV channels ( according OIRT freq plan ) -->"""
print(xmltodict.unparse(root, pretty=True))

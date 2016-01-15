#!/usr/bin/env python

import uhal

hw = uhal.getDevice('testctp7','ipbustcp-2.0://raven1:60002','file://addresses.xml');
hw.getNode('REG').write(77)
reg = hw.getNode('REG').read()
hw.dispatch()
print 'REG',reg.value(),'expect:77'
print

hw.getNode('MEM').writeBlock([1,2,3,4])
reg = hw.getNode('MEM').readBlock(4)
hw.dispatch()
print 'MEM',list(reg),'expect:[1,2,3,4]'
print

hw.getNode('FIFO').writeBlock([1,2,3,4])
reg = hw.getNode('FIFO').readBlock(4)
hw.dispatch()
print 'FIFO',list(reg),'expect:[4,4,4,4]'
print

hw.getNode('REG').write(0x01234567)
reg1 = hw.getNode('REG').read()
reg1m = hw.getNode('REGMASK').read()
hw.getNode('REGMASK').write(0x090b0d0f)
reg2 = hw.getNode('REG').read()
reg2m = hw.getNode('REGMASK').read()
hw.getNode('REG').write(0x87654321)
reg3 = hw.getNode('REG').read()
reg3m = hw.getNode('REGMASK').read()
hw.dispatch()

print 'reg1 ',  '0x{0:08x}'.format(reg1.value()),  'expect:0x01234567'
print 'reg1m', '0x{0:08x}'.format(reg1m.value()), 'expect:0x01030507'
print 'reg2 ',  '0x{0:08x}'.format(reg2.value()),  'expect:0x092b4d6f'
print 'reg2m', '0x{0:08x}'.format(reg2m.value()), 'expect:0x090b0d0f'
print 'reg3 ',  '0x{0:08x}'.format(reg3.value()),  'expect:0x87654321'
print 'reg3m', '0x{0:08x}'.format(reg3m.value()), 'expect:0x07050301'
print

hw.getNode('REG').write(0x01234567)
reg1 = hw.getNode('REG').read()
hw.getClient().rmw_bits(0x60000000, 0xffff0000, 0x11111111)
reg2 = hw.getNode('REG').read()
hw.dispatch()

print 'reg1 ',  '0x{0:08x}'.format(reg1.value()),  'expect:0x01234567'
print 'reg2 ',  '0x{0:08x}'.format(reg2.value()),  'expect:0x11331111'
print

hw.getNode('REG').write(0x01234567)
reg1 = hw.getNode('REG').read()
hw.getClient().rmw_sum(0x60000000, 1)
reg2 = hw.getNode('REG').read()
hw.dispatch()

print 'reg1 ',  '0x{0:08x}'.format(reg1.value()),  'expect:0x01234567'
print 'reg2 ',  '0x{0:08x}'.format(reg2.value()),  'expect:0x01234568'
print

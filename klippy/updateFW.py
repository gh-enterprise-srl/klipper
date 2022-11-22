#!/usr/bin/env python2
# Script to implement a test console with firmware over serial port
#
# Copyright (C) 2016-2021  Kevin O'Connor <kevin@koconnor.net>
#
# This file may be distributed under the terms of the GNU GPLv3 license.
import sys, optparse, os, re, logging
import util, reactor, serialhdl, pins, msgproto, clocksync,time

re_eval = re.compile(r'\{(?P<eval>[^}]*)\}')

class FWUpdater:
    def __init__(self, reactor, serialport, baud, filename):
        self.serialport = serialport
        self.baud = baud
        self.ser = serialhdl.SerialReader(reactor)
        self.reactor = reactor
        self.start_time = reactor.monotonic()
        self.clocksync = clocksync.ClockSync(self.reactor)
        self.fd = sys.stdin.fileno()
        util.set_nonblock(self.fd)
        self.mcu_freq = 0
        self.pins = pins.PinResolver(validate_aliases=False)
        self.data = ""
        self.filename = filename
        #reactor.register_fd(self.fd, self.process_kbd)
        reactor.register_callback(self.connect)

        
        self.eval_globals = {}
    def connect(self, eventtime):
        self.output("CONNECTING")
        self.ser.connect_uart(self.serialport, self.baud)
        msgparser = self.ser.get_msgparser()
        message_count = len(msgparser.get_messages())
        version, build_versions = msgparser.get_version_info()
        #self.output("Loaded %d commands (%s / %s)"
        #            % (message_count, version, build_versions))
        #self.output("MCU config: %s" % (" ".join(
        #    ["%s=%s" % (k, v) for k, v in msgparser.get_constants().items()])))
        self.clocksync.connect(self.ser)
        self.ser.handle_default = self.handle_default
        self.ser.register_response(self.handle_output, '#output')
        self.mcu_freq = msgparser.get_constant_float('CLOCK_FREQ')
        self.output("CONNECTED,VERSION="+version)
        self.updateFW(self.filename)
        return self.reactor.NEVER
    def output(self, msg):
        sys.stdout.write("%s\n" % (msg,))
        sys.stdout.flush()
    def handle_default(self, params):
        tdiff = params['#receive_time'] - self.start_time
        msg = self.ser.get_msgparser().format_params(params)
        self.output("%07.3f: %s" % (tdiff, msg))
    def handle_output(self, params):
        tdiff = params['#receive_time'] - self.start_time
        self.output("%07.3f: %s: %s" % (tdiff, params['#name'], params['#msg']))
    def handle_suppress(self, params):
        pass
    def update_evals(self, eventtime):
        self.eval_globals['freq'] = self.mcu_freq
        self.eval_globals['clock'] = self.clocksync.get_clock(eventtime)
    def updateFW(self, filename):
        self.output("OPENING_FILE")
        fw = open(filename, 'r')
        lines = fw.readlines()
        self.output("PROGRAMMING")
        for line in lines:
            self.ser.send_with_response(line, "fw_update_response")
        self.output("FW_UPDATED")
        exit(0)

    def translate(self, line, eventtime):
        evalparts = re_eval.split(line)
        if len(evalparts) > 1:
            self.update_evals(eventtime)
            try:
                for i in range(1, len(evalparts), 2):
                    e = eval(evalparts[i], dict(self.eval_globals))
                    if type(e) == type(0.):
                        e = int(e)
                    evalparts[i] = str(e)
            except:
                self.output("Unable to evaluate: %s" % (line,))
                return None
            line = ''.join(evalparts)
            self.output("Eval: %s" % (line,))
        try:
            line = self.pins.update_command(line).strip()
        except:
            self.output("Unable to map pin: %s" % (line,))
            return None
        if line:
            parts = line.split()
            if parts[0] in self.local_commands:
                self.local_commands[parts[0]](parts)
                return None
        return line

def main():
    usage = "%prog [options] <serialdevice>"
    opts = optparse.OptionParser(usage)
    opts.add_option("-b", "--baud", type="int", dest="baud", help="baud rate")
    options, args = opts.parse_args()
    if len(args) != 2:
        opts.error("Incorrect number of arguments")
    serialport = args[0]

    baud = options.baud
    if baud is None and not (serialport.startswith("/dev/rpmsg_")
                             or serialport.startswith("/tmp/")):
        baud = 250000

    debuglevel = logging.CRITICAL
    logging.basicConfig(level=debuglevel)

    r = reactor.Reactor()

    filename = args[1]
    kbd = FWUpdater(r, serialport, baud, filename)
    try:
        r.run()
    except KeyboardInterrupt:
        sys.stdout.write("\n")

if __name__ == '__main__':
    main()

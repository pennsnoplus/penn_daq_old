# reads in the configuration file "config" and parses it

cfgfile = "penn_daq.cfg"
deffile = "penn_daq.h"

import re
try:
	# open the config file
    config = open(cfgfile, "r")
    config_lines = config.readlines()
    config.close()

    # open penn_daq.h
    penn_daq = open(deffile, "r")
    penn_daq_lines = penn_daq.readlines()
    penn_daq.close()
    def_begin = [line.strip() for line in penn_daq_lines].index("//_!_DEFINITIONS_!_")
    def_end = [line.strip() for line in penn_daq_lines].index("//_!_END_DEFINTIONS_!_")
    count = def_begin+1
    penn_daq_lines = penn_daq_lines[0:def_begin+1]+penn_daq_lines[def_end:]

    if not def_begin:
        raise Exception, "missing //_!_DEFINITIONS_!_ line in penn_daq.h"
    if not def_end:
        raise Exception, "missing //_!_END_DEFINTIONS_!_ line in penn_daq.h"

    for _line in config_lines:
        out = "\n"
        line = _line.strip()
        if not line:
            out = "\n"
        elif line[0] != "#" and line: # comment
            try:
                match = re.match(r'(?P<name>.+?)(\s*)=(\s*)(?P<value>.+)', line)
                out = "#define %s %s\n" % (match.group('name').upper(), match.group('value'))
            except Exception, err:
                print "penn_daq_gen: Incorrect formatting in %s on line %d" % (cfgfile, config_lines.index(_line))
                print "---- %s" % line
                out = "\n"
        else:
            out = re.sub("#", "//", line)+"\n"
        penn_daq_lines.insert(count, out)
        count += 1

    # open outfile
    of = open(deffile, "w")
    for line in penn_daq_lines:
        of.write(line)
    of.close()
    print "penn_daq_gen: updated penn_daq.h with the correct defines"
except Exception, err:
    print "penn_daq_gen: error:", err
    print "Check to make sure that %s and %s both exist" % (cfgfile, deffile)

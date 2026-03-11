# Split UF2 file so that the last sector is in separate file.

import sys

data = open(sys.argv[1], 'rb').read()
open(sys.argv[2], 'wb').write(data[:-512])
open(sys.argv[3], 'wb').write(data[-512:])
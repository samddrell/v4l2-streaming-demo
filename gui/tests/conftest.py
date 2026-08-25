import sys
from pathlib import Path

# Modules under test (yuy2.py, gui.py, zmq_receiver.py, telemetry_pb2.py once
# generated) live in gui/, one level up from gui/tests/.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

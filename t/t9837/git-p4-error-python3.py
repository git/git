import os
import sys
from  importlib.machinery import SourceFileLoader

def main():
    if len(sys.argv[1:]) != 1:
        print("Expected test directory name")

    gitp4_path = sys.argv[1] + "/../git-p4.py"
    gitp4 = SourceFileLoader("gitp4", gitp4_path).load_module()
    gitp4.p4CmdList(["edit", b'\xFEfile'])

if __name__ == '__main__':
    main()


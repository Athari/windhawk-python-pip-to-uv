import os
import sys
import subprocess

# os.system(sys.executable + " -m pip --version")
# os.system(sys.executable + " -m pip --help")
# os.system(sys.executable + " -m pip list")
# os.system(sys.executable + " -m pip list --python 1")
# os.system(sys.executable + " -m pip install --dry-run numpy")
# os.system(sys.executable + " -m pip install numpy --dry-run")

# subprocess.run([sys.executable, "-m", "pip", "--version"])

dir = os.path.dirname(sys.executable)
os.environ["PATH"] = dir + ";" + dir + "/scripts;" + os.environ["PATH"]
# print(os.environ["PATH"])

os.system("python pip --version")
os.system("python pip.exe --version")
os.system("python " + dir + "/scripts/pip --version")
os.system("python " + dir + "/scripts/pip.exe --version") # only this one works without mod

# os.system("python -m pip --version")
# os.system("python -m pip --help")
# os.system("python -m pip list")
# os.system("python -m pip list --python 1")
# os.system("python -m pip install --dry-run numpy")
# os.system("python -m pip install numpy --dry-run")

# subprocess.run(["python", "-m", "pip", "--version"])

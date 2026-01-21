import os
import sys
import subprocess

# For mod testing purposes only, do not use in production :)

python_dir = os.path.dirname(sys.executable)

os.system(python_dir + "/scripts/pip --version")
os.system(python_dir + "/scripts/pip.exe --version")
os.system(sys.executable + " -m pip --version")
os.system(sys.executable + " pip --version") # doesn't work without mod
os.system(sys.executable + " pip.exe --version") # doesn't work without mod
os.system(sys.executable + " " + python_dir + "/scripts/pip --version") # doesn't work without mod
os.system(sys.executable + " " + python_dir + "/scripts/pip.exe --version")

# os.system(sys.executable + " -m pip --version")
# os.system(sys.executable + " -m pip --help")
# os.system(sys.executable + " -m pip list")
# os.system(sys.executable + " -m pip list --python 1")
# os.system(sys.executable + " -m pip install --dry-run numpy")
# os.system(sys.executable + " -m pip install numpy --dry-run")

# subprocess.run([sys.executable, "-m", "pip", "--version"])

os.environ["PATH"] = python_dir + ";" + python_dir + "/scripts;" + os.environ["PATH"]
# print(os.environ["PATH"])

os.system(python_dir + "/scripts/pip --version")
os.system(python_dir + "/scripts/pip.exe --version")
os.system("python -m pip --version") # doesn't work without mod
os.system("python pip --version") # doesn't work without mod
os.system("python pip.exe --version") # doesn't work without mod
os.system("python " + python_dir + "/scripts/pip --version") # doesn't work without mod
os.system("python " + python_dir + "/scripts/pip.exe --version")

# os.system("python -m pip --version")
# os.system("python -m pip --help")
# os.system("python -m pip list")
# os.system("python -m pip list --python 1")
# os.system("python -m pip install --dry-run numpy")
# os.system("python -m pip install numpy --dry-run")

# subprocess.run(["python", "-m", "pip", "--version"])

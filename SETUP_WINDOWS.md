Windows setup (brief)

Follow these steps to set up ESP-IDF and build locally on Windows (PowerShell):

1) Install prerequisites
   - Install Python 3.10+ from https://www.python.org (ensure "Add Python to PATH" is checked).
   - Install Git for Windows: https://git-scm.com/download/win

2) Install ESP-IDF
   - Open PowerShell as Administrator
   - Clone ESP-IDF (pick a stable release or `release/v5.x`):
     git clone --recursive https://github.com/espressif/esp-idf.git
     cd esp-idf
   - Run the installer script:
     ./install.ps1
   - Once installation completes, run the export script to set env vars for this session:
     .\export.ps1
   - (Optional) Add the export script to your PowerShell profile so IDF is configured for new shells.

3) Verify environment
   - idf.py --version
   - python --version

4) Build this project
   - From the project root (this repo):
     idf.py set-target esp32
     idf.py build

5) Flash & Monitor
   - idf.py flash -p <PORT>
   - idf.py monitor

Notes & troubleshooting
- If `idf.py` is not found after running `export.ps1`, ensure your PowerShell session ran the export script and that Python is in PATH.
- If requirements are missing, run:
  python -m pip install --user -r $env:IDF_PATH\requirements.txt

If you want, run these commands and paste the `idf.py build` output here and I’ll fix compile errors in the repo.
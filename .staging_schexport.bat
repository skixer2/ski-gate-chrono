@echo off
set "K=C:\Progra~1\KiCad\10.0\bin\kicad-cli.exe"
set "P=C:\sgc\pcb\sgc_p1"
"%K%" sch export pdf -o "%P%\sgc_p1_review.pdf" "%P%\sgc_p1.kicad_sch"
echo PDF_EXIT_%errorlevel%
"%K%" sch erc -o "%P%\sgc_p1_erc.rpt" --severity-all "%P%\sgc_p1.kicad_sch"
echo ERC_EXIT_%errorlevel%

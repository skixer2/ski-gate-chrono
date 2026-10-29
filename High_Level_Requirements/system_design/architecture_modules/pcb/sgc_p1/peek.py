src = open("sgc_p1.kicad_sch", encoding="utf-8", errors="ignore").read()
i = src.find("(lib_id")
print(repr(src[max(0,i-40):i+160]))

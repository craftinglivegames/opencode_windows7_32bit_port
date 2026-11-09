import pe_imports, sys
imports, _ = pe_imports.parse(r"C:\work\AppData\Roaming\npm\node_modules\opencode-ai\bin\opencode.exe")
for dll, funcs in imports:
    if dll.lower() in ("kernel32.dll","api-ms-win-core-synch-l1-2-0.dll","user32.dll","userenv.dll","ntdll.dll","shell32.dll","oleaut32.dll"):
        if dll.lower()=="kernel32.dll":
            print("kernel32.dll:", len(funcs), "imports")
            print("  ", ", ".join(funcs))
        else:
            print(dll+":", ", ".join(funcs))

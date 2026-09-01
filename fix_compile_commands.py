from pathlib import Path
import re

p = Path("build/compile_commands.json")

text = p.read_text(encoding="utf-8")

text = re.sub(r'(?i)(?<![a-z])C:', 'c:', text)

p.write_text(text, encoding="utf-8")

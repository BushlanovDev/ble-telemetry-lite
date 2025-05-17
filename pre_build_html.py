import subprocess
import os

html_source_path = os.path.join("data", "index.html")
header_output_path = os.path.join("src", "index_html.h")

os.makedirs(os.path.dirname(header_output_path), exist_ok=True)

print(f"Converting {html_source_path} to {header_output_path}")

try:
    subprocess.run(["xxd", "-i", html_source_path, header_output_path], check=True)
    print(f"Successfully converted {html_source_path} to {header_output_path}")
except FileNotFoundError:
    print("Error: xxd command not found. Please install xxd.")
    exit(1)
except subprocess.CalledProcessError as e:
    print(f"Error during xxd execution: {e}")
    exit(1)

with open(header_output_path, 'r') as f:
    content = f.read()

array_name = os.path.basename(html_source_path).replace('.', '_') # например, index_html
if html_source_path.startswith("data" + os.path.sep):
    array_name = "data_" + array_name # например, data_index_html

content = content.replace(f"unsigned char {array_name}[] = {{",
                          f"#pragma once\n\nconst unsigned char {array_name}[] PROGMEM = {{")

with open(header_output_path, 'w') as f:
    f.write(content)

print(f"Modified {header_output_path} for PROGMEM.")

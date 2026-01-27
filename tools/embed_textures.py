# Converts PNG files to C++ byte arrays for embedding
# Usage: python embed_textures.py <input_dir> <output_file>

import sys
import os
from PIL import Image

def png_to_rgba(filepath):
    """Convert PNG to RGBA byte array"""
    img = Image.open(filepath).convert('RGBA')
    return img.size[0], img.size[1], list(img.tobytes())

def sanitize_name(name):
    """Convert filename to valid C++ identifier"""
    return name.replace('.', '_').replace('-', '_').replace(' ', '_')

def generate_header(input_dir, output_file):
    header = """#pragma once
// Auto-generated item texture data
// Do not edit manually

struct EmbeddedTexture {
    int width;
    int height;
    const unsigned char* data;
    unsigned int dataSize;
};

"""
    
    textures = {}
    texture_decls = []
    
    items_dir = os.path.join(input_dir, 'items')
    if not os.path.exists(items_dir):
        items_dir = input_dir
    
    for filename in sorted(os.listdir(items_dir)):
        if not filename.endswith('.png'):
            continue
        
        filepath = os.path.join(items_dir, filename)
        try:
            width, height, data = png_to_rgba(filepath)
        except Exception as e:
            print(f"Skipping {filename}: {e}")
            continue
        
        name = sanitize_name(os.path.splitext(filename)[0])
        var_name = f"tex_{name}"
        
        # Generate byte array
        header += f"// {filename} ({width}x{height})\n"
        header += f"static const unsigned char {var_name}_data[] = {{\n    "
        
        for i, b in enumerate(data):
            if i > 0 and i % 16 == 0:
                header += "\n    "
            header += f"0x{b:02x},"
        
        header += "\n};\n\n"
        
        textures[name] = (var_name, width, height, len(data))
    
    # Generate lookup table
    header += "// Texture lookup by name\n"
    header += "static const struct { const char* name; EmbeddedTexture tex; } embeddedTextures[] = {\n"
    
    for name, (var_name, w, h, size) in textures.items():
        header += f'    {{"{name}", {{{w}, {h}, {var_name}_data, {size}}}}},\n'
    
    header += "    {nullptr, {0, 0, nullptr, 0}}\n"
    header += "};\n"
    
    with open(output_file, 'w') as f:
        f.write(header)
    
    print(f"Generated {output_file} with {len(textures)} textures")

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: python embed_textures.py <input_dir> <output_file>")
        sys.exit(1)
    
    generate_header(sys.argv[1], sys.argv[2])

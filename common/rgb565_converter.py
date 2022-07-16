#!/usr/bin/python3
import argparse
import os
from PIL import Image


def main():
    parser = argparse.ArgumentParser(
        description="Convert a file from one format to another."
    )
    parser.add_argument(
        "-i",
        "--input",
        required=True,
        dest="input_directory",
        help="Input directory to be converted."
    )
    parser.add_argument(
        "-o",
        "--output",
        nargs="?",
        dest="output_file",
        help="Output file to be converted."
    )
    args = parser.parse_args()


    output_basename = os.path.basename(args.output_file).rsplit('.', 1)

    if len(output_basename) != 2:
        print("Error: Invalid arguments.")
        exit(1)

    with open(args.output_file, 'w') as output_file:
        output_file.write("#include <assets.h> \n\n")

    path_of_the_directory= args.input_directory
    for filename in os.listdir(path_of_the_directory):
        f = os.path.join(path_of_the_directory,filename)
        if os.path.isfile(f):
            extension = os.path.basename(f).rsplit('.', 1)[1]
            if extension == "png":
                print("Converting: "+f)
                convert_png_to_rgb565(args.output_file, f)


def convert_png_to_rgb565(output_file, input_file):
    array_name = os.path.basename(input_file).rsplit('.', 1)[0]
    png = Image.open(input_file)
    width, height = png.size

    max_line_width = min(width, 25)

    # iterate over the pixels
    image = png.getdata()
    image_content = ""
    for i, pixel in enumerate(image):
        r = (pixel[0] >> 3) & 0x1F
        g = (pixel[1] >> 2) & 0x3F
        b = (pixel[2] >> 3) & 0x1F
        rgb = r << 11 | g << 5 | b
        image_content += f"0x{rgb:04X}" + (",\n        " if (i % max_line_width == max_line_width-1) else ",")

    if image_content.endswith("\n    "):
        image_content = image_content[:-5]

    output_cpp_content = f"""


const asset_t {array_name} ICACHE_RODATA_ATTR =
{{
    {width}, {height},
    {{
        {image_content}
    }}
}};
    """.strip() + "\n"

    with open(output_file, 'a') as output_file:
        output_file.write(output_cpp_content)


if __name__ == '__main__':
    main()

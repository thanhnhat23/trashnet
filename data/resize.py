import os
from pathlib import Path
from PIL import Image
import constants

def resize(image, dim1, dim2):
    return image.resize((dim2, dim1), Image.Resampling.LANCZOS)

def fileWalk(directory, destPath):
    directory = Path(directory)
    destPath = Path(destPath)

    if not directory.exists():
        print(f"[!] Source directory not found: {directory}")
        return

    destPath.mkdir(parents=True, exist_ok=True)

    count = 0
    valid_extensions = ('.jpg', '.jpeg', '.png', '.bmp', '.webp')

    for entry in os.scandir(directory):
        if not entry.is_file():
            continue

        file_path = Path(entry.path)
        if file_path.suffix.lower() not in valid_extensions:
            continue

        target_file = destPath / file_path.name

        try:
            # Read and resize the image, then save it to the destination path
            with Image.open(file_path) as pic:
                if pic.mode != "RGB":
                    pic = pic.convert("RGB")

                if pic.height > pic.width:
                    pic = pic.rotate(90, expand=True)

                picResized = resize(pic, constants.DIM1, constants.DIM2)
                picResized.save(target_file, format="JPEG", quality=95)

            # Remove the original file after successful processing
            file_path.unlink()
            count += 1

        except Exception as e:
            print(f"[!] Error processing {file_path.name}: {e}")

    print(f"[+] Resized and removed {count} images from {directory.name} -> {destPath}")

def main():
    base_dir = Path(__file__).resolve().parent
    prepath = base_dir / 'dataset-original'
    destPath = base_dir / 'dataset-resized'

    categories = ['glass', 'paper', 'cardboard', 'plastic', 'metal', 'trash', 'battery', 'biological']

    for category in categories:
        src = prepath / category
        dst = destPath / category
        fileWalk(src, dst)

if __name__ == '__main__':
    main()
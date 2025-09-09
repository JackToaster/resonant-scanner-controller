import argparse
import cv2
import sys

import numpy as np

from enc_dec import encode_rle, decode_rle

def parse_arguments():
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(description="Load and display an image using OpenCV.")
    parser.add_argument(
        "image_path",
        type=str,
        help="Path to the image file to be loaded."
    )
    parser.add_argument(
        "store_path",
        type=str,
        help="Path to store the RLE encoded image."
    )
    parser.add_argument(
        "--show",
        action="store_true",
        help="Display the image in a window."
    )
    parser.add_argument(
        "--video",
        action="store_true",
        help="Open as video file."
    )
    parser.add_argument(
        "--invert",
        action="store_true",
        help="Invert threshold."
    )
    parser.add_argument(
        "--size",
        type=int,
        nargs=2,
        metavar=("WIDTH", "HEIGHT"),
        help="Resize image to the given WIDTH and HEIGHT."
    )
    return parser.parse_args()

def load_image(image_path):
    """Load an image from the given path."""
    image = cv2.imread(image_path, cv2.IMREAD_GRAYSCALE)
    if image is None:
        print(f"Error: Could not load image from {image_path}")
        sys.exit(1)
    return image

def rle_encode_img(image, size, invert=False, show=False):
    if len(image.shape) > 2:
        # color image, convert to bw
        image = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)

    resized = cv2.resize(image, size)

    if show:
        cv2.imshow("Scaled", resized)

    if invert:
        _, threshed = cv2.threshold(resized, 127, 255, cv2.THRESH_BINARY_INV)
    else:
        _, threshed = cv2.threshold(resized, 127, 255, cv2.THRESH_BINARY)

    encoded = encode_rle(threshed)
    if show:
        decoded = decode_rle(encoded)
        print(f"Decoded size: {len(decoded)}")
        decoded_img = np.reshape(decoded, (64,64))
        cv2.imshow("Binary", threshed)
        cv2.imshow("decoded", decoded_img)
        cv2.waitKey(0)
        cv2.destroyAllWindows()

    return encoded


def main():
    args = parse_arguments()
    if args.video:
        cap = cv2.VideoCapture(args.image_path)
        frameno = 0
        with open(args.store_path, 'wb') as f:
            while cap.isOpened():
                try:
                    _, frame = cap.read()
                except cv2.error as e:
                    print(e)
                    break
                if frame is None:
                    print("done!")
                    break
                encoded = rle_encode_img(frame, args.size, args.invert, args.show)
                f.write(encoded)
                print(f"Frame {frameno}: {len(encoded)} bytes")
                frameno += 1

    else:
        image = load_image(args.image_path)

        print(f"Image loaded successfully: {args.image_path}")
        print(f"Dimensions: {image.shape[1]}x{image.shape[0]} (width x height)")

        encoded = rle_encode_img(image, args.size, args.invert, args.show)

        with open(args.store_path, 'wb') as f:
            f.write(encoded)

if __name__ == "__main__":
    main()

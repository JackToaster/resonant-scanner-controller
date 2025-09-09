import numpy as np

def enc_bytes(run_length):
    bts = bytearray()
    if run_length == 0:
        bts.extend(bytes([run_length]))
        return bts
    while run_length > 255:
        bts.extend(bytes([255,0]))
        run_length -= 255
    if run_length > 0:
        bts.extend(bytes([run_length]))
    return bts

def encode_rle(image):
    flat_img = image.flatten()

    output = bytearray()
    light = False
    count = 0
    for pixel in flat_img:
        if light:
            if pixel == 255:
                count += 1
            else:
                output.extend(enc_bytes(count))
                count = 1
                light = False
        else:
            if pixel == 0:
                count += 1
            else:
                output.extend(enc_bytes(count))
                count = 1
                light = True

    output.extend(enc_bytes(count))
    # if light:
    #     output.extend(enc_bytes(0)) # reset to dark at end of frame (or start of next frame)
    return output

def decode_rle(bytes: bytearray):
    output = np.array([], dtype=np.uint8)
    light = False
    for byte in bytes:
        if light:
            if byte != 0:
                output = np.concat((output, np.full((int(byte),), 255, dtype=np.uint8)))
            light = False
        else:
            if byte != 0:
                output = np.concat((output, np.full((int(byte),), 0, dtype=np.uint8)))
            light = True
    return output
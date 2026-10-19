import hashlib
import io
import struct
import zipfile

from libpebble2.services.putbytes import PutBytes, PutBytesType
from pebble_tool.commands.base import PebbleCommand
from pebble_tool.exceptions import ToolError

# File the AS7000 driver flashes at boot (CONFIG_HRM_AS7000_FW_UPDATE)
WATCH_FILENAME = "as7000fw"

APP_SPACE = 0x8000
MARKER_ADDRESS = 0x7FFC
MARKER_LEN = 4

# Images seen in stock Pebble 2 HR firmware, by sha256
KNOWN_IMAGES = {
    "a50541daa1b57c31b9a92072226df69cb07d9827122d27f08787ebe0609928e8": "stock 4.2 to 4.4.3",
    "2eddfdfe9b0e01550c47c15cb1fface957d15e60da3387ae16ffbc9554167ded": "factory, stock v4.0 recovery",
}

PBPACK_MANIFEST_LEN = 12
PBPACK_ENTRY_LEN = 16
# Stock 4.x packs have 512 table entries, current system packs 768, app packs 256
PBPACK_TABLE_SIZES = (512, 768, 256)


def parse_image(data):
    """Return the version string of an AS7000 image, or None if it is not one.

    The image is the version (two bytes, as in the sensor's information
    registers) followed by segments of {address, length - 1, data}, the last
    one being the valid application marker.
    """
    if len(data) < 2 + 4 + MARKER_LEN or data[0] >> 6 == 0:
        return None
    cursor = 2
    address = length = None
    while cursor < len(data):
        if cursor + 4 > len(data):
            return None
        address, length_minus_1 = struct.unpack_from("<HH", data, cursor)
        length = length_minus_1 + 1
        if address + length > APP_SPACE:
            return None
        cursor += 4 + length
    if cursor != len(data) or address != MARKER_ADDRESS or length != MARKER_LEN:
        return None
    return f"{data[0] >> 6}.{data[0] & 0x3F}.{data[1]}"


def find_in_pbpack(pack):
    (num_files,) = struct.unpack_from("<I", pack, 0)
    for table_size in PBPACK_TABLE_SIZES:
        content_start = PBPACK_MANIFEST_LEN + table_size * PBPACK_ENTRY_LEN
        if num_files > table_size:
            continue
        for i in range(num_files):
            entry = PBPACK_MANIFEST_LEN + i * PBPACK_ENTRY_LEN
            if entry + PBPACK_ENTRY_LEN > len(pack):
                break
            _, offset, length, _ = struct.unpack_from("<IIII", pack, entry)
            start = content_start + offset
            image = pack[start : start + length]
            if len(image) == length and parse_image(image):
                return image
    return None


def extract_image(path):
    with open(path, "rb") as f:
        data = f.read()
    if parse_image(data):
        return data
    if zipfile.is_zipfile(io.BytesIO(data)):
        with zipfile.ZipFile(io.BytesIO(data)) as bundle:
            for name in bundle.namelist():
                if name.endswith(".pbpack"):
                    image = find_in_pbpack(bundle.read(name))
                    if image:
                        return image
        return None
    return find_in_pbpack(data)


class InstallHrmFwCommand(PebbleCommand):
    """Send a Pebble 2 HR sensor (AS7000) firmware image to the watch.

    The image comes from a stock Pebble firmware bundle (.pbz), its system
    resources (.pbpack) or a raw image file. The watch flashes it onto the
    sensor at its next boot if the sensor runs another version, and shows the
    outcome in Settings > System > Information > "HRM update". The image is
    not redistributable, so use a firmware you obtained yourself.
    """

    command = "install-hrm-fw"

    def __call__(self, args):
        image = extract_image(args.source)
        if image is None:
            raise ToolError(f"No AS7000 sensor image found in {args.source}")

        digest = hashlib.sha256(image).hexdigest()
        print(
            f"AS7000 image {parse_image(image)}, {len(image)} bytes, sha256 {digest}"
            f" ({KNOWN_IMAGES.get(digest, 'unknown origin')})"
        )
        if args.output:
            with open(args.output, "wb") as f:
                f.write(image)
            print(f"Saved to {args.output}")
            return

        super().__call__(args)
        PutBytes(
            self.pebble, PutBytesType.File, image, bank=0, filename=WATCH_FILENAME
        ).send()
        print(
            "Sent. Reboot the watch to update the sensor; the heart rate sensor"
            " is unavailable for up to a minute while it flashes."
        )

    @classmethod
    def add_parser(cls, parser):
        parser = super().add_parser(parser)
        parser.add_argument(
            "source", help="Stock firmware (.pbz), system resources (.pbpack) or image"
        )
        parser.add_argument(
            "--output",
            metavar="FILE",
            help="Save the extracted image instead of sending it",
        )
        return parser

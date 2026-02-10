from Crypto.PublicKey import ECC
from Crypto.Cipher import AES
from Crypto.Random import get_random_bytes
from Crypto.Hash import SHA256
from Crypto.Protocol.KDF import HKDF
import struct
import argparse

def derive_shared_key(private_key: ECC.EccKey, peer_public_key: ECC.EccKey) -> bytes:
    shared_point =  peer_public_key.pointQ * private_key.d
    shared_key_bytes = int(shared_point.x).to_bytes(48, 'little')
    
    derived_key = HKDF(
        shared_key_bytes, 
        32, 
        salt=b'', 
        hashmod=SHA256, 
        context=b'ecc-aes-gcm-key'
    )
    if isinstance(derived_key, bytes):
        return derived_key
    else:
        return b''.join(derived_key)


def aes_gcm_encrypt(plaintext: bytes, key: bytes) -> bytes:
    nonce = get_random_bytes(12)
    cipher = AES.new(key, AES.MODE_GCM, nonce=nonce)
    ciphertext, tag = cipher.encrypt_and_digest(plaintext)
    
    return nonce + tag + ciphertext


def aes_gcm_decrypt(ciphertext: bytes, key: bytes) -> bytes:
    nonce = ciphertext[:12]
    tag = ciphertext[12: 12+16]
    cipher = AES.new(key, AES.MODE_GCM, nonce=nonce)
    try:
        plaintext = cipher.decrypt_and_verify(ciphertext[12+16:], tag)
        return plaintext
    except ValueError as e:
        print(f"❌ decrypt failed: {e}")
        return b''


def ecc_encrypt(plaintext: bytes, public_key: ECC.EccKey) -> bytes:
    temp_private = ECC.generate(curve='P-384')
    temp_public_bin = temp_private.public_key().export_key(format='DER')
    data = bytearray()
    data += struct.pack('>H', len(temp_public_bin))
    data += temp_public_bin
    shared_key = derive_shared_key(temp_private, public_key)
    data += aes_gcm_encrypt(plaintext, shared_key)
    return bytes(data)


def ecc_decrypt(ciphertext: bytes, private_key: ECC.EccKey) -> bytes:
    key_len = struct.unpack('>H', ciphertext[:2])[0]
    temp_public = ECC.import_key(ciphertext[2: 2+key_len])
    shared_key = derive_shared_key(private_key, temp_public)
    return aes_gcm_decrypt(ciphertext[2+key_len:], shared_key)


if __name__ == "__main__":
    parser = argparse.ArgumentParser('tool for ecc+aes encrypt/decrypt')
    parser.add_argument('--encrypt', help='public key for encrypt')
    parser.add_argument('--decrypt', help='public key for decrypt')
    parser.add_argument('input', help='need to encrypt/decrypt file')
    parser.add_argument('output', help='output file')
    args = parser.parse_args()
    input_data = open(args.input, 'rb').read()
    if args.encrypt:
        with open(args.output, 'wb') as f:
            f.write(ecc_encrypt(input_data, ECC.import_key(open(args.encrypt, 'rb').read())))
    elif args.decrypt:
        with open(args.output, 'wb') as f:
            f.write(ecc_decrypt(input_data, ECC.import_key(open(args.decrypt).read())))

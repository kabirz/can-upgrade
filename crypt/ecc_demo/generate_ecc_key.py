
from Crypto.PublicKey import ECC

if __name__ == "__main__":
    key = ECC.generate(curve='P-384')
    with open('ecc_private.pem', 'w') as f:
        f.write(key.export_key(format='PEM'))
    with open('ecc_public.der', 'wb') as f:
        f.write(key.public_key().export_key(format='DER'))

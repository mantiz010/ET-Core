"""Authenticated legacy string compatibility and accurate rejection reasons."""
import asyncio,base64,hashlib,unittest
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
from harness import make_hub

class PayloadDiagnostics(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.key='ab'*32
        self.hub=make_hub(asyncio.get_running_loop(),self.key)
        self.session=b'1234567'
        self.hub._devices['test']={'id':'test','rx_session':self.session.hex(),'rx_seq':0}

    def packet(self,plain,key=None,seq=1):
        nonce=b'\x01'+self.session+seq.to_bytes(4,'big')
        cipher=ChaCha20Poly1305(hashlib.sha256(bytes.fromhex(key or self.key)+b'test').digest())
        data=cipher.encrypt(nonce,plain,f'v1|state|test|{seq}'.encode())
        enc=lambda b:base64.b64encode(b).decode()
        return {'type':'state','id':'test','seq':seq,'_src_ip':'127.0.0.2','payload':{'_enc':1,'n':enc(nonce),'ct':enc(data[:-16]),'tag':enc(data[-16:])}}

    def test_legacy_control_characters_preserved_after_authentication(self):
        for n in range(32):
            p=self.packet(b'{"message":"before'+bytes([n])+b'after"}',seq=n+1)
            self.assertTrue(self.hub._decode_payload(p))
            self.assertEqual(p['payload']['message'],'before'+chr(n)+'after')
        self.assertEqual(self.hub._decrypt_fail_count,0)

    def test_wrong_key_is_authentication_failure(self):
        self.assertFalse(self.hub._decode_payload(self.packet(b'{}',key='cd'*32)))
        self.assertEqual(self.hub._bad_sources['127.0.0.2']['why'],'authentication_failed')

    def test_authenticated_invalid_json_is_payload_failure(self):
        for data in [b'{"x":',b'[]',b'{\x02"x":1}',b'{"x":"\xff"}']:
            self.assertFalse(self.hub._decode_payload(self.packet(data)))
            self.assertEqual(self.hub._bad_sources['127.0.0.2']['why'],'invalid_payload')

    def test_bad_envelope_is_format_failure(self):
        p=self.packet(b'{}');p['payload']['n']='bad'
        self.assertFalse(self.hub._decode_payload(p))
        self.assertEqual(self.hub._bad_sources['127.0.0.2']['why'],'invalid_envelope')

    def test_legacy_message_replay_is_still_rejected(self):
        self.assertTrue(self.hub._decode_payload(self.packet(b'{"message":"a\x02b"}')))
        self.assertFalse(self.hub._decode_payload(self.packet(b'{"message":"a\x02b"}')))
        self.assertEqual(self.hub._replay_count,1)

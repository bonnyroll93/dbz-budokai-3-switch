"""Switch capture validation and build inputs from the project shader folder."""
import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

GENERATOR = Path(__file__).parents[1] / 'gen-deko3d-shaders.py'
spec = importlib.util.spec_from_file_location('shader_generator', GENERATOR)
g = importlib.util.module_from_spec(spec)
spec.loader.exec_module(g)

VS = (b'struct VSInput { float4 a0 : ATTR0; };\n'
      b'struct VSOutput { float4 pos : SV_POSITION; };\n'
      b'VSOutput main(VSInput input) { VSOutput o; o.pos=input.a0; return o; }\n')
PS = (b'struct PSInput { float4 col0 : COLOR0; };\n'
      b'float4 main(PSInput input) : SV_TARGET { return input.col0; }\n')


class ShaderPrebuildTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.sdk = self.root / 'video'
        self.hlsl = self.root / 'shaders' / 'deko3d' / 'hlsl'
        self.hlsl.mkdir(parents=True)
        self.captures = self.root / 'switch-run'
        self.captures.mkdir()
        keys = ('ROOT', 'HLSL_DIR', 'GLSL_DIR', 'HEADER', 'ENGINE_C', 'STAMP')
        self.previous = {key: getattr(g, key) for key in keys}
        self.addCleanup(lambda: [setattr(g, key, value) for key, value in self.previous.items()])
        g.ROOT = str(self.root)
        g.HLSL_DIR = str(self.hlsl)
        g.GLSL_DIR = str(self.sdk / 'deko3d' / 'glsl')
        g.HEADER = str(self.sdk / 'deko3d' / 'rsx_deko3d_shaders.h')
        g.ENGINE_C = str(self.sdk / 'rsx_draw_engine.c')
        g.STAMP = str(self.root / 'generated' / 'stamp.json')
        self.sdk.mkdir()
        Path(g.ENGINE_C).write_text(
            'static const char* const kEngFixedVS = "' +
            VS.decode().replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n"\n"') +
            '";\nstatic const char* const kEngFixedPS = "' +
            PS.decode().replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n"\n"') +
            '";\n', encoding='ascii')

    def test_switch_capture_imports_into_project_folder(self):
        name = 'vs_%016x.hlsl' % g.fnv1a64(VS)
        self.captures.joinpath(name).write_bytes(VS)
        self.assertEqual(g.import_capture_dirs([self.captures]), 1)
        self.assertEqual(self.hlsl.joinpath(name).read_bytes(), VS)
        self.assertEqual(g.import_capture_dirs([self.captures]), 0)

    def test_capture_rejects_wrong_hash(self):
        self.captures.joinpath('ps_0000000000000000.hlsl').write_bytes(PS)
        with self.assertRaises(g.TranslateError):
            g.import_capture_dirs([self.captures])
        self.assertEqual(list(self.hlsl.iterdir()), [])

    def test_pc_trace_is_not_accepted(self):
        self.captures.joinpath('pipe1.ps.hlsl').write_bytes(PS)
        with self.assertRaises(g.TranslateError):
            g.import_capture_dirs([self.captures])

    def test_build_reads_project_folder_and_is_incremental(self):
        captured = PS.replace(b'return input.col0;', b'return input.col0 * 0.5;')
        name = 'ps_%016x.hlsl' % g.fnv1a64(captured)
        self.hlsl.joinpath(name).write_bytes(captured)
        uam = self.root / 'uam.exe'
        uam.write_bytes(b'fake tool')
        calls = []

        def fake_uam(command, **kwargs):
            calls.append(command)
            Path(command[command.index('-o') + 1]).write_bytes(b'FAKE_DKSH_BLOB')
            return subprocess.CompletedProcess(command, 0, '', '')

        argv = ['generator', '--uam', str(uam), '--incremental']
        with patch.object(sys, 'argv', argv), patch.object(subprocess, 'run', side_effect=fake_uam):
            self.assertEqual(g.main(), 0)
            self.assertEqual(len(calls), 3)
            self.assertIn(b'rsx_dk_shader_table', Path(g.HEADER).read_bytes())
            self.assertEqual(g.main(), 0)
            self.assertEqual(len(calls), 3)

    def test_failed_compilation_keeps_previous_header(self):
        target = Path(g.HEADER)
        target.parent.mkdir(parents=True)
        target.write_bytes(b'KNOWN_GOOD_HEADER')
        uam = self.root / 'uam.exe'
        uam.write_bytes(b'fake tool')
        result = subprocess.CompletedProcess([], 1, '', 'compiler rejected shader')
        with patch.object(sys, 'argv', ['generator', '--uam', str(uam)]), \
             patch.object(subprocess, 'run', return_value=result):
            self.assertEqual(g.main(), 1)
        self.assertEqual(target.read_bytes(), b'KNOWN_GOOD_HEADER')


if __name__ == '__main__':
    unittest.main()

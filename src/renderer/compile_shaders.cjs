/* Optional shader regeneration. Run from this directory with Node.js.
 * DXC: third_party/shaders/dxc-1.9.2609/bin/x64/dxc.exe (Windows).
 * GLSL: pinned @webgpu/glslang 0.0.15 WASM compiler. Runtime needs neither. */
const fs=require('fs');
const path=require('path');
const cp=require('child_process');
const root=path.resolve(__dirname,'../..');
const dxc=process.env.DXC||path.join(root,'third_party/shaders/dxc-1.9.2609/bin/x64/dxc.exe');
for(const [stage,target] of [['vert','vs_6_0'],['frag','ps_6_0']]) {
    cp.execFileSync(dxc,['-T',target,'-E','main',path.join(__dirname,'thrash.'+stage+'.hlsl'),'-Fo',path.join(__dirname,'thrash.'+stage+'.dxil')]);
}
let glslang;
try { glslang=require.resolve('@webgpu/glslang'); }
catch (_) { glslang=path.join(root,'third_party/shaders/glslang-0.0.15/package/dist/node-devel/glslang.js'); }
const compiler=require(glslang)();
for(const [stage,kind] of [['vert','vertex'],['frag','fragment']]) {
    const file=path.join(__dirname,'thrash.'+stage);
    const spv=compiler.compileGLSL(fs.readFileSync(file+'.glsl','utf8'),kind);
    fs.writeFileSync(file+'.spv',Buffer.from(spv.buffer));
}
let output='/* Generated from local shader artifacts. See compile_shaders.cjs. */\n';
for(const [name,file] of [['vs_dxil','thrash.vert.dxil'],['ps_dxil','thrash.frag.dxil'],['vs_spv','thrash.vert.spv'],['ps_spv','thrash.frag.spv'],['msl','thrash.metal']]) {
    const bytes=fs.readFileSync(path.join(__dirname,file));output+='static const unsigned char '+name+'[] = {\n';
    for(let off=0;off<bytes.length;off+=24) output+=Array.from(bytes.subarray(off,off+24)).join(',')+',\n';
    output+='0};\n';
}
fs.writeFileSync(path.join(__dirname,'nfspu_shaders.h'),output);

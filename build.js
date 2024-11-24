import fs from 'fs';
import JSZip from 'jszip';

const version = '0.2.0';

console.log('Building');
let ide_data = fs.readFileSync('./.pio/build/esp32c3supermini/idedata.json', {encoding: 'utf-8'});
let json = JSON.parse(ide_data);

const addresses = [];
const files = {};

console.log(json.extra);

json.extra.flash_images.forEach(image => {
    const filename = image.path.split('\\').pop();
    files[filename] = fs.readFileSync(image.path, {encoding:'binary'});

    addresses.push({
        address: parseInt(image.offset),
        filename: filename
    });
})

files['firmware.bin'] = fs.readFileSync('./.pio/build/esp32c3supermini/firmware.bin', {encoding:'binary'});

addresses.push({
    address: parseInt(json.extra.application_offset),
    filename: 'firmware.bin'
});

addresses.sort((a, b) => a.address < b.address ? -1 : 1);

const result = {
    firmware: version,
    addresses: addresses
}

console.log(result);

const archive = new JSZip();
archive.file('firmware.json', JSON.stringify(result, undefined, 4));

result.addresses.forEach((file, f) => {
    console.log(file.filename, '0x' + ('00000000' + file.address.toString(16)).slice(-8));
    archive.file(file.filename, files[file.filename], { binary: true });
});
const buffer = await archive.generateAsync({type: 'nodebuffer' })
fs.writeFileSync(`./.pio/build/${version}.zip`, buffer);



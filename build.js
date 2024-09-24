import fs from 'fs';
import AdmZip from 'adm-zip';

const version = '0.2.0';

console.log('Building');
let idedata = fs.readFileSync('./.pio/build/esp32c3supermini/idedata.json', {encoding: 'utf-8'});
let json = JSON.parse(idedata);
const offsets = [];
const files = [];
console.log(json.extra);

json.extra.flash_images.forEach(image => {
    files.push(fs.readFileSync(image.path, {encoding:'binary'}));
    const filename = image.path.split('\\').pop();
    offsets.push({
        offset: image.offset,
        filename: filename
    });
})

files.push(fs.readFileSync('./.pio/build/esp32c3supermini/firmware.bin', {encoding:'binary'}));

offsets.push({
    offset: json.extra.application_offset,
    filename: 'firmware.bin'
});

const result = {
    firmware: version,
    offsets: offsets
}

console.log(result);

const zip = new AdmZip();
zip.addFile('firmware.json', Buffer.from(JSON.stringify(result, undefined, 4), 'utf-8') );
result.offsets.forEach((file, f) => {
    console.log(file.filename)
    zip.addFile(file.filename, files[f]);
});
zip.writeZip(`./.pio/build/${version}.zip`);


var BVL1 = 0, BVL2 = 1, BVL3 = 2, BVL4 = 3,
    BVL5 = 4, BVL6 = 5, BVL7 = 6, BVL8 = 7,
    power_of_ten = [
  1, 10, 100, 1000, 10000, 100000, 1e+06, 1e+07, 1e+08, 1e+09, 1e+10,
  1e+11, 1e+12, 1e+13, 1e+14, 1e+15, 1e+16, 1e+17, 1e+18, 1e+19, 1e+20,
  1e+21, 1e+22, 1e+23, 1e+24, 1e+25, 1e+26, 1e+27, 1e+28, 1e+29, 1e+30,
  1e+31, 1e+32, 1e+33, 1e+34, 1e+35, 1e+36, 1e+37, 1e+38, 1e+39, 1e+40,
  1e+41, 1e+42, 1e+43, 1e+44, 1e+45, 1e+46, 1e+47, 1e+48, 1e+49, 1e+50,
  1e+51, 1e+52, 1e+53, 1e+54, 1e+55, 1e+56, 1e+57, 1e+58, 1e+59, 1e+60,
  1e+61, 1e+62, 1e+63, 1e+64, 1e+65, 1e+66, 1e+67, 1e+68, 1e+69, 1e+70,
  1e+71, 1e+72, 1e+73, 1e+74, 1e+75, 1e+76, 1e+77, 1e+78, 1e+79, 1e+80,
  1e+81, 1e+82, 1e+83, 1e+84, 1e+85, 1e+86, 1e+87, 1e+88, 1e+89, 1e+90,
  1e+91, 1e+92, 1e+93, 1e+94, 1e+95, 1e+96, 1e+97, 1e+98, 1e+99, 1e+100,
  1e+101, 1e+102, 1e+103, 1e+104, 1e+105, 1e+106, 1e+107, 1e+108, 1e+109,
  1e+110, 1e+111, 1e+112, 1e+113, 1e+114, 1e+115, 1e+116, 1e+117, 1e+118,
  1e+119, 1e+120, 1e+121, 1e+122, 1e+123, 1e+124, 1e+125, 1e+126, 1e+127,
  1e-128, 1e-127, 1e-126, 1e-125, 1e-124, 1e-123, 1e-122, 1e-121, 1e-120,
  1e-119, 1e-118, 1e-117, 1e-116, 1e-115, 1e-114, 1e-113, 1e-112, 1e-111,
  1e-110, 1e-109, 1e-108, 1e-107, 1e-106, 1e-105, 1e-104, 1e-103, 1e-102,
  1e-101, 1e-100, 1e-99, 1e-98, 1e-97, 1e-96,
  1e-95, 1e-94, 1e-93, 1e-92, 1e-91, 1e-90, 1e-89, 1e-88, 1e-87, 1e-86,
  1e-85, 1e-84, 1e-83, 1e-82, 1e-81, 1e-80, 1e-79, 1e-78, 1e-77, 1e-76,
  1e-75, 1e-74, 1e-73, 1e-72, 1e-71, 1e-70, 1e-69, 1e-68, 1e-67, 1e-66,
  1e-65, 1e-64, 1e-63, 1e-62, 1e-61, 1e-60, 1e-59, 1e-58, 1e-57, 1e-56,
  1e-55, 1e-54, 1e-53, 1e-52, 1e-51, 1e-50, 1e-49, 1e-48, 1e-47, 1e-46,
  1e-45, 1e-44, 1e-43, 1e-42, 1e-41, 1e-40, 1e-39, 1e-38, 1e-37, 1e-36,
  1e-35, 1e-34, 1e-33, 1e-32, 1e-31, 1e-30, 1e-29, 1e-28, 1e-27, 1e-26,
  1e-25, 1e-24, 1e-23, 1e-22, 1e-21, 1e-20, 1e-19, 1e-18, 1e-17, 1e-16,
  1e-15, 1e-14, 1e-13, 1e-12, 1e-11, 1e-10, 1e-09, 1e-08, 1e-07, 1e-06,
  1e-05, 0.0001, 0.001, 0.01, 0.1
    ];

var decode_bucket = function(hist, buffer, buffer_len, offset, raw) {
  var remaining = buffer_len - offset;
  if(remaining < 3) return -1;
  var key,
      bucket_val = buffer.readInt8(offset),
      bucket_exp = buffer.readUInt8(offset + 1),
      tgt_type = buffer.readUInt8(offset + 2);
  if(tgt_type > BVL8) return -1;
  if(remaining < 3 + tgt_type + 1) return -1;
  var count = 0;
  for(var i=tgt_type;i>=0;i--) {
    count += buffer.readUInt8(offset + i + 3) * (Math.pow(2,i*8));
  }
  if(raw) {
    key = buffer.slice(offset,offset+2).toString('hex');
  }
  else {
    if(bucket_val > 99 || bucket_val < -99) key = 'NaN';
    else if(bucket_val < 10 && bucket_val > -10) key = '0.0';
    else key = ((bucket_val/10.0) * power_of_ten[bucket_exp]).toString();
  }
  hist[key] = count;
  return 3 + tgt_type + 1;
};

exports.decode = function(b64) {
  var raw = false;
  if(arguments[1] != undefined) {
    if(typeof(arguments[1]) != 'boolean') {
      throw ('Invalid non-boolean second argument to histogram.decode');
    }
    raw = arguments[1];
  }

  var b = new Buffer(b64, 'base64'),
      blen = b.length,
      offset = 0,
      hist = {};

  if(b.length < 2) return null;
  var cnt = b.readInt16BE(offset);
  if(cnt == 0) return hist;
  offset += 2;
  while(offset < blen && cnt > 0) {
    var incr_read = decode_bucket(hist, b, blen, offset, raw);
    if(incr_read <= 0) break;
    offset += incr_read;
    cnt--;
  }
  return hist;
};

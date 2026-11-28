# Function index

Every function a script can call, with the page that describes it. Functions marked "always available" need no
`use`; the others come with the library named in the second column. The last column is the name the function has when it is written as a method of its first argument
(`"abc".upper()` for `str_upper("abc")`; see [method calls](language.md#104-method-calls-on-built-in-values)), and is empty for a function that has no such form.

<!-- BEGIN GENERATED: function-index -->
_This table is generated from the engine's library tables by `tools/docs.py`. Change the source, then run `python3 tools/docs.py generate`._

| Function | Library | Method |
| :--- | :--- | :--- |
| [`args`](builtins.md) | always available |  |
| [`assert`](builtins.md) | always available |  |
| [`contains`](builtins.md) | always available | `contains` |
| [`copy`](builtins.md) | always available | `copy` |
| [`count`](builtins.md) | always available | `count` |
| [`csv_parse`](dlc/csv.md) | `DLC:csv` |  |
| [`csv_parse_records`](dlc/csv.md) | `DLC:csv` |  |
| [`csv_write`](dlc/csv.md) | `DLC:csv` |  |
| [`csv_write_records`](dlc/csv.md) | `DLC:csv` |  |
| [`datetime_format`](dlc/datetime.md) | `DLC:datetime` |  |
| [`datetime_make`](dlc/datetime.md) | `DLC:datetime` |  |
| [`datetime_offset`](dlc/datetime.md) | `DLC:datetime` |  |
| [`datetime_parse`](dlc/datetime.md) | `DLC:datetime` |  |
| [`datetime_parts`](dlc/datetime.md) | `DLC:datetime` |  |
| [`exit`](builtins.md) | always available |  |
| [`file_add`](dlc/filesystem.md) | `DLC:filesystem` |  |
| [`file_copy`](dlc/filesystem.md) | `DLC:filesystem` |  |
| [`file_exist`](dlc/filesystem.md) | `DLC:filesystem` |  |
| [`file_isdir`](dlc/filesystem.md) | `DLC:filesystem` |  |
| [`file_list`](dlc/filesystem.md) | `DLC:filesystem` |  |
| [`file_makedir`](dlc/filesystem.md) | `DLC:filesystem` |  |
| [`file_move`](dlc/filesystem.md) | `DLC:filesystem` |  |
| [`file_read`](dlc/filesystem.md) | `DLC:filesystem` |  |
| [`file_readlines`](dlc/filesystem.md) | `DLC:filesystem` |  |
| [`file_remove`](dlc/filesystem.md) | `DLC:filesystem` |  |
| [`file_removedir`](dlc/filesystem.md) | `DLC:filesystem` |  |
| [`file_size`](dlc/filesystem.md) | `DLC:filesystem` |  |
| [`file_write`](dlc/filesystem.md) | `DLC:filesystem` |  |
| [`find`](builtins.md) | always available | `find` |
| [`find_all`](builtins.md) | always available | `find_all` |
| [`from_json`](dlc/json.md) | `DLC:json` | `from_json` |
| [`hash_base64_decode`](dlc/hash.md) | `DLC:hash` |  |
| [`hash_base64_encode`](dlc/hash.md) | `DLC:hash` |  |
| [`hash_base64url_decode`](dlc/hash.md) | `DLC:hash` |  |
| [`hash_base64url_encode`](dlc/hash.md) | `DLC:hash` |  |
| [`hash_crc32`](dlc/hash.md) | `DLC:hash` |  |
| [`hash_equal`](dlc/hash.md) | `DLC:hash` |  |
| [`hash_from_bytes`](dlc/hash.md) | `DLC:hash` |  |
| [`hash_hex_decode`](dlc/hash.md) | `DLC:hash` |  |
| [`hash_hex_encode`](dlc/hash.md) | `DLC:hash` |  |
| [`hash_hmac_sha256`](dlc/hash.md) | `DLC:hash` |  |
| [`hash_hmac_sha512`](dlc/hash.md) | `DLC:hash` |  |
| [`hash_md5`](dlc/hash.md) | `DLC:hash` |  |
| [`hash_pbkdf2_sha256`](dlc/hash.md) | `DLC:hash` |  |
| [`hash_sha1`](dlc/hash.md) | `DLC:hash` |  |
| [`hash_sha256`](dlc/hash.md) | `DLC:hash` |  |
| [`hash_sha512`](dlc/hash.md) | `DLC:hash` |  |
| [`hash_to_bytes`](dlc/hash.md) | `DLC:hash` |  |
| [`hw_analog`](dlc/hardware.md) | `DLC:hardware` |  |
| [`hw_battery`](dlc/hardware.md) | `DLC:hardware` |  |
| [`hw_beep`](dlc/hardware.md) | `DLC:hardware` |  |
| [`hw_button`](dlc/hardware.md) | `DLC:hardware` |  |
| [`hw_led`](dlc/hardware.md) | `DLC:hardware` |  |
| [`hw_pin`](dlc/hardware.md) | `DLC:hardware` |  |
| [`hw_pwm`](dlc/hardware.md) | `DLC:hardware` |  |
| [`hw_screen_text`](dlc/hardware.md) | `DLC:hardware` |  |
| [`hw_sensor`](dlc/hardware.md) | `DLC:hardware` |  |
| [`hw_serial`](dlc/hardware.md) | `DLC:hardware` |  |
| [`hw_supports`](dlc/hardware.md) | `DLC:hardware` |  |
| [`index_of`](builtins.md) | always available | `index_of` |
| [`input`](builtins.md) | always available |  |
| [`length`](builtins.md) | always available | `length` |
| [`list_average`](dlc/list.md) | `DLC:list` | `average` |
| [`list_flatten`](dlc/list.md) | `DLC:list` | `flatten` |
| [`list_join`](dlc/list.md) | `DLC:list` | `join` |
| [`list_range`](dlc/list.md) | `DLC:list` |  |
| [`list_reverse`](dlc/list.md) | `DLC:list` | `reverse` |
| [`list_sort`](dlc/list.md) | `DLC:list` | `sort` |
| [`list_sum`](dlc/list.md) | `DLC:list` | `sum` |
| [`list_unique`](dlc/list.md) | `DLC:list` | `unique` |
| [`map_entries`](dlc/map.md) | `DLC:map` | `entries` |
| [`map_has_key`](dlc/map.md) | `DLC:map` | `has_key` |
| [`map_keys`](dlc/map.md) | `DLC:map` | `keys` |
| [`map_merge`](dlc/map.md) | `DLC:map` | `merge` |
| [`map_values`](dlc/map.md) | `DLC:map` | `values` |
| [`match`](builtins.md) | always available | `match` |
| [`math_abs`](dlc/math.md) | `DLC:math` | `abs` |
| [`math_acos`](dlc/math.md) | `DLC:math` | `acos` |
| [`math_asin`](dlc/math.md) | `DLC:math` | `asin` |
| [`math_atan`](dlc/math.md) | `DLC:math` | `atan` |
| [`math_atan2`](dlc/math.md) | `DLC:math` | `atan2` |
| [`math_bit_and`](dlc/math.md) | `DLC:math` | `bit_and` |
| [`math_bit_not`](dlc/math.md) | `DLC:math` | `bit_not` |
| [`math_bit_or`](dlc/math.md) | `DLC:math` | `bit_or` |
| [`math_bit_shl`](dlc/math.md) | `DLC:math` | `bit_shl` |
| [`math_bit_shr`](dlc/math.md) | `DLC:math` | `bit_shr` |
| [`math_bit_test`](dlc/math.md) | `DLC:math` | `bit_test` |
| [`math_bit_xor`](dlc/math.md) | `DLC:math` | `bit_xor` |
| [`math_ceil`](dlc/math.md) | `DLC:math` | `ceil` |
| [`math_clamp`](dlc/math.md) | `DLC:math` | `clamp` |
| [`math_cos`](dlc/math.md) | `DLC:math` | `cos` |
| [`math_e`](dlc/math.md) | `DLC:math` |  |
| [`math_exp`](dlc/math.md) | `DLC:math` | `exp` |
| [`math_floor`](dlc/math.md) | `DLC:math` | `floor` |
| [`math_from_base`](dlc/math.md) | `DLC:math` | `from_base` |
| [`math_log`](dlc/math.md) | `DLC:math` | `log` |
| [`math_log10`](dlc/math.md) | `DLC:math` | `log10` |
| [`math_log2`](dlc/math.md) | `DLC:math` | `log2` |
| [`math_max`](dlc/math.md) | `DLC:math` |  |
| [`math_min`](dlc/math.md) | `DLC:math` |  |
| [`math_mod`](dlc/math.md) | `DLC:math` | `mod` |
| [`math_pi`](dlc/math.md) | `DLC:math` |  |
| [`math_pow`](dlc/math.md) | `DLC:math` | `pow` |
| [`math_round`](dlc/math.md) | `DLC:math` | `round` |
| [`math_sign`](dlc/math.md) | `DLC:math` | `sign` |
| [`math_sin`](dlc/math.md) | `DLC:math` | `sin` |
| [`math_sqrt`](dlc/math.md) | `DLC:math` | `sqrt` |
| [`math_tan`](dlc/math.md) | `DLC:math` | `tan` |
| [`math_to_base`](dlc/math.md) | `DLC:math` | `to_base` |
| [`math_trunc`](dlc/math.md) | `DLC:math` | `trunc` |
| [`network_get`](dlc/network.md) | `DLC:network` |  |
| [`network_post`](dlc/network.md) | `DLC:network` |  |
| [`path_basename`](dlc/path.md) | `DLC:path` |  |
| [`path_dirname`](dlc/path.md) | `DLC:path` |  |
| [`path_extension`](dlc/path.md) | `DLC:path` |  |
| [`path_is_absolute`](dlc/path.md) | `DLC:path` |  |
| [`path_join`](dlc/path.md) | `DLC:path` |  |
| [`path_normalize`](dlc/path.md) | `DLC:path` |  |
| [`path_relative`](dlc/path.md) | `DLC:path` |  |
| [`path_split`](dlc/path.md) | `DLC:path` |  |
| [`path_stem`](dlc/path.md) | `DLC:path` |  |
| [`path_with_extension`](dlc/path.md) | `DLC:path` |  |
| [`pattern`](builtins.md) | always available |  |
| [`print`](builtins.md) | always available |  |
| [`print_error`](builtins.md) | always available |  |
| [`random_choice`](dlc/random.md) | `DLC:random` |  |
| [`random_float`](dlc/random.md) | `DLC:random` |  |
| [`random_int`](dlc/random.md) | `DLC:random` |  |
| [`random_seed`](dlc/random.md) | `DLC:random` |  |
| [`random_shuffle`](dlc/random.md) | `DLC:random` |  |
| [`replace`](builtins.md) | always available | `replace` |
| [`replace_all`](builtins.md) | always available | `replace_all` |
| [`split`](builtins.md) | always available | `split` |
| [`str_char_code`](dlc/string.md) | `DLC:string` | `char_code` |
| [`str_ends_with`](dlc/string.md) | `DLC:string` | `ends_with` |
| [`str_from_char_code`](dlc/string.md) | `DLC:string` |  |
| [`str_lower`](dlc/string.md) | `DLC:string` | `lower` |
| [`str_pad_left`](dlc/string.md) | `DLC:string` |  |
| [`str_pad_right`](dlc/string.md) | `DLC:string` |  |
| [`str_repeat`](dlc/string.md) | `DLC:string` | `repeat` |
| [`str_starts_with`](dlc/string.md) | `DLC:string` | `starts_with` |
| [`str_trim`](dlc/string.md) | `DLC:string` | `trim` |
| [`str_trim_end`](dlc/string.md) | `DLC:string` | `trim_end` |
| [`str_trim_start`](dlc/string.md) | `DLC:string` | `trim_start` |
| [`str_upper`](dlc/string.md) | `DLC:string` | `upper` |
| [`task_done`](builtins.md) | always available | `done` |
| [`time_now`](dlc/time.md) | `DLC:time` |  |
| [`time_sleep`](dlc/time.md) | `DLC:time` |  |
| [`time_timestamp`](dlc/time.md) | `DLC:time` |  |
| [`to_boolean`](dlc/convert.md) | `DLC:convert` |  |
| [`to_boolean`](builtins.md) | always available | `to_boolean` |
| [`to_json`](dlc/json.md) | `DLC:json` | `to_json` |
| [`to_number`](dlc/convert.md) | `DLC:convert` |  |
| [`to_number`](builtins.md) | always available | `to_number` |
| [`to_str`](dlc/convert.md) | `DLC:convert` |  |
| [`to_str`](builtins.md) | always available | `to_str` |
| [`type_of`](builtins.md) | always available |  |
<!-- END GENERATED: function-index -->

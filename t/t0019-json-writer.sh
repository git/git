#!/bin/sh

test_description='test json-writer JSON generation'
. ./test-lib.sh

test_expect_success 'unit test of json-writer routines' '
	test-tool json-writer -u
'

test_expect_success 'trivial object' '
	cat >expect <<-\EOF &&
	{}
	EOF
	cat >input <<-\EOF &&
	object
	end
	EOF
	test-tool json-writer <input >actual &&
	test_cmp expect actual
'

test_expect_success 'trivial array' '
	cat >expect <<-\EOF &&
	[]
	EOF
	cat >input <<-\EOF &&
	array
	end
	EOF
	test-tool json-writer <input >actual &&
	test_cmp expect actual
'

test_expect_success 'simple object' '
	cat >expect <<-\EOF &&
	{"a":"abc","b":42,"c":3.14,"d":true,"e":false,"f":null}
	EOF
	cat >input <<-\EOF &&
	object
		object-string a abc
		object-int b 42
		object-double c 2 3.140
		object-true d
		object-false e
		object-null f
	end
	EOF
	test-tool json-writer <input >actual &&
	test_cmp expect actual
'

test_expect_success 'simple array' '
	cat >expect <<-\EOF &&
	["abc",42,3.14,true,false,null]
	EOF
	cat >input <<-\EOF &&
	array
		array-string abc
		array-int 42
		array-double 2 3.140
		array-true
		array-false
		array-null
	end
	EOF
	test-tool json-writer <input >actual &&
	test_cmp expect actual
'

test_expect_success 'escape quoting string' '
	cat >expect <<-\EOF &&
	{"a":"abc\\def"}
	EOF
	cat >input <<-\EOF &&
	object
		object-string a abc\def
	end
	EOF
	test-tool json-writer <input >actual &&
	test_cmp expect actual
'

test_expect_success 'escape quoting string 2' '
	cat >expect <<-\EOF &&
	{"a":"abc\"def"}
	EOF
	cat >input <<-\EOF &&
	object
		object-string a abc"def
	end
	EOF
	test-tool json-writer <input >actual &&
	test_cmp expect actual
'

test_expect_success 'nested inline object' '
	cat >expect <<-\EOF &&
	{"a":"abc","b":42,"sub1":{"c":3.14,"d":true,"sub2":{"e":false,"f":null}}}
	EOF
	cat >input <<-\EOF &&
	object
		object-string a abc
		object-int b 42
		object-object sub1
			object-double c 2 3.140
			object-true d
			object-object sub2
				object-false e
				object-null f
			end
		end
	end
	EOF
	test-tool json-writer <input >actual &&
	test_cmp expect actual
'

test_expect_success 'nested inline array' '
	cat >expect <<-\EOF &&
	["abc",42,[3.14,true,[false,null]]]
	EOF
	cat >input <<-\EOF &&
	array
		array-string abc
		array-int 42
		array-array
			array-double 2 3.140
			array-true
			array-array
				array-false
				array-null
			end
		end
	end
	EOF
	test-tool json-writer <input >actual &&
	test_cmp expect actual
'

test_expect_success 'nested inline object and array' '
	cat >expect <<-\EOF &&
	{"a":"abc","b":42,"sub1":{"c":3.14,"d":true,"sub2":[false,null]}}
	EOF
	cat >input <<-\EOF &&
	object
		object-string a abc
		object-int b 42
		object-object sub1
			object-double c 2 3.140
			object-true d
			object-array sub2
				array-false
				array-null
			end
		end
	end
	EOF
	test-tool json-writer <input >actual &&
	test_cmp expect actual
'

test_expect_success 'nested inline object and array 2' '
	cat >expect <<-\EOF &&
	{"a":"abc","b":42,"sub1":{"c":3.14,"d":true,"sub2":[false,{"g":0,"h":1},null]}}
	EOF
	cat >input <<-\EOF &&
	object
		object-string a abc
		object-int b 42
		object-object sub1
			object-double c 2 3.140
			object-true d
			object-array sub2
				array-false
				array-object
					object-int g 0
					object-int h 1
				end
				array-null
			end
		end
	end
	EOF
	test-tool json-writer <input >actual &&
	test_cmp expect actual
'

test_expect_success 'pretty nested inline object and array 2' '
	sed -e "s/^|//" >expect <<-\EOF &&
	|{
	|  "a": "abc",
	|  "b": 42,
	|  "sub1": {
	|    "c": 3.14,
	|    "d": true,
	|    "sub2": [
	|      false,
	|      {
	|        "g": 0,
	|        "h": 1
	|      },
	|      null
	|    ]
	|  }
	|}
	EOF
	cat >input <<-\EOF &&
	object
		object-string a abc
		object-int b 42
		object-object sub1
			object-double c 2 3.140
			object-true d
			object-array sub2
				array-false
				array-object
					object-int g 0
					object-int h 1
				end
				array-null
			end
		end
	end
	EOF
	test-tool json-writer -p <input >actual &&
	test_cmp expect actual
'

test_expect_success 'inline object with no members' '
	cat >expect <<-\EOF &&
	{"a":"abc","empty":{},"b":42}
	EOF
	cat >input <<-\EOF &&
	object
		object-string a abc
		object-object empty
		end
		object-int b 42
	end
	EOF
	test-tool json-writer <input >actual &&
	test_cmp expect actual
'

test_expect_success 'inline array with no members' '
	cat >expect <<-\EOF &&
	{"a":"abc","empty":[],"b":42}
	EOF
	cat >input <<-\EOF &&
	object
		object-string a abc
		object-array empty
		end
		object-int b 42
	end
	EOF
	test-tool json-writer <input >actual &&
	test_cmp expect actual
'

test_expect_success 'larger empty example' '
	cat >expect <<-\EOF &&
	{"a":"abc","empty":[{},{},{},[],{}],"b":42}
	EOF
	cat >input <<-\EOF &&
	object
		object-string a abc
		object-array empty
			array-object
			end
			array-object
			end
			array-object
			end
			array-array
			end
			array-object
			end
		end
		object-int b 42
	end
	EOF
	test-tool json-writer <input >actual &&
	test_cmp expect actual
'

test_lazy_prereq PERLJSON '
	perl -MJSON -e "exit 0"
'

# As a sanity check, ask Perl to parse our generated JSON and recursively
# dump the resulting data in sorted order.  Confirm that that matches our
# expectations.
test_expect_success PERLJSON 'parse JSON using Perl' '
	cat >expect <<-\EOF &&
	row[0].a abc
	row[0].b 42
	row[0].sub1 hash
	row[0].sub1.c 3.14
	row[0].sub1.d 1
	row[0].sub1.sub2 array
	row[0].sub1.sub2[0] 0
	row[0].sub1.sub2[1] hash
	row[0].sub1.sub2[1].g 0
	row[0].sub1.sub2[1].h 1
	row[0].sub1.sub2[2] null
	EOF
	cat >input <<-\EOF &&
	object
		object-string a abc
		object-int b 42
		object-object sub1
			object-double c 2 3.140
			object-true d
			object-array sub2
				array-false
				array-object
					object-int g 0
					object-int h 1
				end
				array-null
			end
		end
	end
	EOF
	test-tool json-writer <input >output.json &&
	perl "$TEST_DIRECTORY"/t0019/parse_json.perl <output.json >actual &&
	test_cmp expect actual
'

test_done

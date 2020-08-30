# Tests

The `tests` folder contains a number of tests in the following form:

- `*.txt` spec files. 
	These files contain descriptions of the syntax and examples. 
	The examples are marked as code-blocks with `example` language.
	The examples all have the same form: first Markdown, then HTML
- `spec_tests.py` is a python script to parse the spec files and extract all example-code-blocks.
	The example-data is then processed using the provided tool and the expected HTML is compared
	
The `scripts` folder contains a `run-tests.sh` that will run all tests with the command line tools found in `/build`. The script is expected to be run from within the `/build` folder
from distutils.core import setup, Extension

_cboar = Extension(
    '_cboar',
    libraries=['m'],
    sources=['source/cboarmodule.c', 'source/encoder.c', 'source/decoder.c']
)

classifiers = ['Development Status :: 5 - Production/Stable',
               'Operating System :: POSIX :: Linux',
               'License :: OSI Approved :: MIT License',
               'Intended Audience :: Developers',
               'Programming Language :: Python :: 3',
               'Topic :: Software Development']

setup(
    name='cboar',
    version='0.1',
    author='Dave Jones',
    author_email='dave@waveform.org.uk',
    description='A high performance, flexible CBOR implementation',
    long_description='',
    license='MIT',
    keywords='CBOR',
    url='https://github.com/waveform80/cboar.git',
    classifiers=classifiers,
    packages=['cboar'],
    ext_modules=[_cboar],
)

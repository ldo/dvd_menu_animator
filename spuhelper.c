/*
	Python extension module for DVD Menu Animator script. This performs
	pixel-level manipulations that would be too slow done in Python.

	Created by Lawrence D'Oliveiro <ldo@geek-central.gen.nz>.
*/

#include <Python.h>
#include <stdbool.h>
#include <stdint.h>

static PyObject * spuhelper_cairo_to_gtk
  (
	PyObject * self,
	PyObject * args
  )
  /* converts a buffer of RGBA-format pixels from Cairo (native-endian) ordering to
	GTK Pixbuf (big-endian) ordering. */
  {
	PyObject * result = 0;
	unsigned long pixaddr, pixlen;
	uint8_t * pixels;
	do /*once*/
	  {
		if (!PyArg_ParseTuple(args, "kk", &pixaddr, &pixlen))
			break;
		pixels = (uint8_t *)pixaddr;
		pixlen >>= 2;
		while (pixlen > 0)
		  {
			const uint32_t thispixel = *(const uint32_t *)pixels;
			*pixels++ = thispixel >> 16 & 255; /* R */
			*pixels++ = thispixel >> 8 & 255; /* G */
			*pixels++ = thispixel & 255; /* B */
			*pixels++ = thispixel >> 24 & 255; /* alpha */
			--pixlen;
		  } /*while*/
	  /* all done */
		Py_INCREF(Py_None);
		result = Py_None;
	  }
	while (false);
	return result;
  } /*spuhelper_cairo_to_gtk*/

static PyMethodDef spuhelper_methods[] =
  {
	{"cairo_to_gtk", spuhelper_cairo_to_gtk, METH_VARARGS,
		"cairo_to_gtk(address, nrpixels)\n"
		"converts a buffer of RGBA-format pixels from Cairo (native-endian) ordering"
		" to GTK Pixbuf (big-endian) ordering."
	},
	{0, 0, 0, 0}
  };

PyMODINIT_FUNC initspuhelper(void)
  {
	(void)Py_InitModule("spuhelper", spuhelper_methods);
  } /*initspuhelper*/

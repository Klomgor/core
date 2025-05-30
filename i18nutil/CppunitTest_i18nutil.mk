For makefiles:

# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t; fill-column: 100 -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_CppunitTest_CppunitTest,i18nutil))

$(eval $(call gb_CppunitTest_use_sdk_api,i18nutil))

$(eval $(call gb_CppunitTest_add_exception_objects,i18nutil,\
	i18nutil/qa/cppunit/test_kashida \
	i18nutil/qa/cppunit/test_scriptchangescanner \
))

$(eval $(call gb_CppunitTest_use_libraries,i18nutil,\
	i18nutil \
	sal \
	test \
))

# vim: set noet sw=4 ts=4:

/*
 * Copyright 2014 Marios Kogias <marioskogias@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 *   1. Redistributions of source code must retain the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer.
 *   2. Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials
 *      provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY COPYRIGHT HOLDERS ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <ztracer.hpp>
#include <cstring>

namespace ZTracer {
	int ztrace_init(void)
	{
		return blkin_init();
	}

	char * to_cstr(string &s)
	{
		char *cstr = new char [s.length()+1];
		strcpy(cstr, s.c_str());

		return cstr;
	}

	char * ostr_to_cstr(ostringstream &stream)
	{
		string s;
		s = stream.str();

		return to_cstr(s);
	}
	int ZTrace::keyval(string key, string val)
	{
                if( ep->get_blkin_ep() ){
		    BLKIN_KEYVAL(&trace, ep->get_blkin_ep(), (char *)key.c_str(),
				(char *)val.c_str());
		}
                return 0;
	}

	int ZTrace::event(string event)
	{
                if( ep->get_blkin_ep() ){
		    BLKIN_TIMESTAMP(&trace, ep->get_blkin_ep(), (char *)event.c_str());
		}
                return 0;
	}

	int ZTrace::keyval(string key, string val, ZTraceEndpointRef ep)
	{
		BLKIN_KEYVAL(&trace, ep->get_blkin_ep(), (char *)key.c_str(),
				(char *)val.c_str());
		return 0;
	}

	int ZTrace::event(string event, ZTraceEndpointRef ep)
	{
		BLKIN_TIMESTAMP(&trace, ep->get_blkin_ep(), (char *)event.c_str());
		return 0;
	}


	ZTraceEndpointRef create_ZTraceEndpoint(string ip, int port, string name)
	{
		boost::shared_ptr<ZTraceEndpoint> ret(new ZTraceEndpoint(ip, port, name));
		return ret;
	}

	ZTraceRef create_ZTrace(string name, ZTraceEndpointRef ep)
	{
		boost::shared_ptr<ZTrace> ret(new ZTrace(name, ep));
		return ret;
	}

	ZTraceRef create_ZTrace(string name, ZTraceRef t)
	{
		boost::shared_ptr<ZTrace> ret(new ZTrace(name, t));
		return ret;
	}

	ZTraceRef create_ZTrace(string name, ZTraceRef t, ZTraceEndpointRef ep)
	{
		boost::shared_ptr<ZTrace> ret(new ZTrace(name, t, ep));
		return ret;
	}

	ZTraceRef create_ZTrace(string name, ZTraceEndpointRef ep, struct blkin_trace_info *info, bool child)
	{
		boost::shared_ptr<ZTrace> ret(new ZTrace(name, ep, info, child));
		return ret;
	}
	int ZTrace::get_trace_info(struct blkin_trace_info *info)
	{
		return blkin_get_trace_info(&trace, info);
	}
	int ZTrace::set_trace_info(struct blkin_trace_info *info)
	{
		return blkin_set_trace_info(&trace, info);
	}
}

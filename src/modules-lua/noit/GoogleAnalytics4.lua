-- Copyright (c) 2010-2017, Circonus, Inc.
-- All rights reserved.
--
-- Redistribution and use in source and binary forms, with or without
-- modification, are permitted provided that the following conditions are
-- met:
--
--     * Redistributions of source code must retain the above copyright
--       notice, this list of conditions and the following disclaimer.
--     * Redistributions in binary form must reproduce the above
--       copyright notice, this list of conditions and the following
--       disclaimer in the documentation and/or other materials provided
--       with the distribution.
--     * Neither the name Circonus, Inc. nor the names of its contributors
--       may be used to endorse or promote products derived from this
--       software without specific prior written permission.
--
-- THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
-- "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
-- LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
-- A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
-- OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
-- SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
-- LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
-- DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
-- THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
-- (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
-- OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

local GoogleAnalytics = { }
GoogleAnalytics.__index = GoogleAnalytics

local HttpClient = require 'mtev.HttpClient'

-- NOTE: New, the v4 API takes POSTed json rather than
--       query string args on the url.
function metric_list_to_json(list)
    local len = # list
    if len == 0 then
        return ''
    end

    local string = '{"expression": "ga:' .. list[1] .. '"}'
    for i = 2, len do
        string = string .. ',' .. '{"expression": "ga:' .. list[i] .. '"}'
    end

    return string
end

function generate_json(view_id, start_date, end_date, metrics)
    local json = '{"reportRequests":['
    local num_rpts = # metrics
    for rpt_idx = 1, num_rpts do
        json = json .. '{' ..
                '"viewId":"' .. view_id .. '",' ..
                '"dateRanges":[{"startDate":"' .. start_date .. '","endDate":"' .. end_date .. '"}],' ..
                '"metrics":[' .. metric_list_to_json(metrics[rpt_idx]) .. ']' ..
                '}'
        if rpt_idx < num_rpts then
            json = json .. ','
        end
    end

    json = json .. ']}'
    return json
end
-- end new for v4

function __genOrderedIndex( t )
    local orderedIndex = {}
    for key in pairs(t) do
        table.insert( orderedIndex, key )
    end
    table.sort( orderedIndex )
    return orderedIndex
end

function orderedNext(t, state)
    if state == nil then
        -- the first time, generate the index
        t.__orderedIndex = __genOrderedIndex( t )
        key = t.__orderedIndex[1]
        return key, t[key]
    end

    -- fetch the next value
    key = nil
    for i = 1,table.getn(t.__orderedIndex) do
        if t.__orderedIndex[i] == state then
            key = t.__orderedIndex[i+1]
        end
    end

    if key then
        return key, t[key]
    end

    -- no more value to return, cleanup
    t.__orderedIndex = nil
    return
end

function orderedPairs(t)
    return orderedNext, t, nil
end

-- function gastrjoin(delimiter, list)
--     local len = # list
--     if len == 0 then
--         return ''
--     end
--     local string = 'ga:' .. list[1]
--     for i = 2, len do
--         string = string .. delimiter .. 'ga:' .. list[i]
--     end
--     return string
-- end

function escape(s)
    s = string.gsub(s, "([%%,:/&=+%c])", function (c)
        return string.format("%%%02X", string.byte(c))
    end)
    s = string.gsub(s, " ", "+")
    return s
end

function constructOAuthBaseString(baseTable)
    local baseurl = 'https://analyticsreporting.googleapis.com/v4/reports:batchGet'
    local toReturn = ''

    for key, value in orderedPairs(baseTable) do
        if key == "enddate" then
            key = "end-date"
        elseif key == "startdate" then
            key = "start-date"
        end

        toReturn =  toReturn .. key .. '=' .. escape(value) .. '&'
    end

    toReturn = string.sub(toReturn, 1, -2)
    toReturn = escape(toReturn)
    toReturn = 'GET&' .. escape(baseurl) .. '&' .. toReturn

    return toReturn
end

function GoogleAnalytics:new(params, hooks)
    local obj = { }
    setmetatable(obj, GoogleAnalytics)
    obj.params = params or { }
    obj.hooks = hooks or { }
    -- obj.metrics = gastrjoin(',', params.metrics or { })
    return obj
end

function dump(o)
   if type(o) == 'table' then
      local s = '{ '
      for k,v in pairs(o) do
         if type(k) ~= 'number' then k = '"'..k..'"' end
         s = s .. '['..k..'] = ' .. dump(v) .. ','
      end
      return s .. '} '
   else
      return tostring(o)
   end
end

function GoogleAnalytics:perform(target, cache_table, api_key, client_id, client_secret)
    -- local baseurl = 'https://www.googleapis.com/analytics/v2.4/data?'
    local baseurl = 'https://analyticsreporting.googleapis.com/v4/reports:batchGet?'
    local port = 443
    local use_ssl = true
    local username = self.params.username
    local password = self.params.password
    -- local table_id = self.params.table_id
    local view_id = self.params.table_id -- v4 'table id' is view id

    local use_oauth = self.params.use_oauth
    local oauth_token = self.params.oauth_token
    local oauth_token_secret = self.params.oauth_token_secret
    local oauth_version = self.params.oauth_version or "1.0"

    if oauth_version == "NONE" then
        -- This implies that we're using an old token and that the
        -- user doesn't have access to it.... treat it like a 1.0
        -- token
        oauth_version = "1.0"
    end

    if use_oauth == nil then
        use_oauth = 'false'
    end

    if oauth_token == nil then
        oauth_token = 'false'
    end

    if oauth_token_secret == nil then
        oauth_token_secret = 'false'
    end

    local start_date = '2005-01-01'
    local end_date = os.date("%Y-%m-%d", os.time()+172800)

    -- old code from v2 api calls
    -- local metrics = self.metrics
    -- table_id = 'ga:' .. table_id
    -- local url = baseurl .. 'ids=' .. table_id ..
    --            '&start-date=' .. start_date .. '&end-date=' .. end_date ..
    --            '&metrics=' .. metrics

    local url = baseurl
    local output = ''

    local urlencode = function (t)
        local localescape = function (s)
            s = string.gsub(s, "([&=+%c])", function (c)
                    return string.format("%%%02X", string.byte(c))
                end)
            s = string.gsub(s, " ", "+")
            return s
        end
        local s = ""
        for k,v in pairs(t) do
            s = s .. "&" .. localescape(k) .. "=" .. localescape(v)
        end
        return string.sub(s, 2)     -- remove first `&'
    end

    local gdata_auth = ''

    -- callbacks from the HttpClient
    local callbacks = { }
    callbacks.consume = function (str) output = output .. str end
    local client = HttpClient:new(callbacks)
    local rv, err = client:connect(target, port, use_ssl)

    if rv ~= 0 then
        return rv, err
    end

    local headers = {}

    -- only login if the user is not using oauth
    if use_oauth == nil or use_oauth == 'false' then
        -- perform the auth request
        headers['Content-Type'] = 'application/x-www-form-urlencoded';
        local auth_post_data = {
            Email = username,
            Passwd = password,
            accountType = 'GOOGLE',
            source = 'curl-dataFeed-v2',
            service = 'analytics',
            key = api_key
        }
        client:do_request("POST", "/accounts/ClientLogin", headers, urlencode(auth_post_data))
        client:get_response()
        local line
        for line in string.gmatch(output, "([^\r\n]+)") do
            if line:find("^Auth=") ~= nil then
                gdata_auth = 'GoogleLogin ' .. line
            elseif line:find("^Error=CaptchaRequired") ~= nil then
                gdata_auth = 'CaptchaRequired'
            end
        end
        output = ''

        if gdata_auth == 'CaptchaRequired' then
            return -1, 'Captcha Required for ' .. username
        elseif gdata_auth == '' then
            return -1, 'Authorization Unsuccessful for ' .. username
        end
    elseif oauth_version == "2.0" then
        local new_auth_token = ''
        headers.Host = 'accounts.google.com'
        headers['Content-Type'] = 'application/x-www-form-urlencoded'
        headers['GData-Version'] = "2"
        local auth_post_data = {
            refresh_token = oauth_token,
            client_id     = client_id,
            client_secret = client_secret,
            grant_type    = 'refresh_token'
        }
        client:do_request("POST", "/o/oauth2/token", headers, urlencode(auth_post_data))
        client:get_response()
        local jsondoc = mtev.parsejson(output)
        local data = jsondoc:document()
        local err = ''
        for k, v in pairs(data) do
            if k == "access_token" then
                new_auth_token = v
            elseif k == 'error' then
                err = err .. v
            elseif k == 'err_description' then
                err = err .. ' - ' .. v
            end
        end
        -- expose any auth errors
        if err ~= '' then
            return -1, err
        end
        output = ''
        url = url .. "&access_token=" .. new_auth_token
    elseif oauth_version == "1.0" then
        local oauth_timestamp = os.time()
        local oauth_nonce = mtev.base64_encode(math.random(1000000000))
        local base_string = ''
        local signature = ''
        local hmac_key = 'anonymous&' .. oauth_token_secret

        url = url ..
            '&oauth_version=1.0&oauth_consumer_key=anonymous&oauth_signature_method=HMAC-SHA1' ..
            '&oauth_timestamp=' .. oauth_timestamp ..
            '&oauth_nonce=' .. oauth_nonce ..
            '&oauth_token=' .. oauth_token ..
            "&key=" .. api_key

        baseTable = {
            oauth_consumer_key = 'anonymous',
            oauth_nonce = oauth_nonce,
            oauth_signature_method = 'HMAC-SHA1',
            oauth_timestamp = oauth_timestamp,
            oauth_version = '1.0',
            oauth_token = oauth_token,
            -- ids = table_id,
            -- metrics = metrics,
            -- startdate = start_date,
            -- enddate = end_date,
            key = api_key
        }

        base_string = constructOAuthBaseString(baseTable)
        signature = escape(mtev.hmac_sha1_encode(base_string, hmac_key))

        if signature == nil then
            signature = 'false'
        end

        url = url .. '&oauth_signature=' .. signature
    end

    local schema, host, uri = string.match(url, "^(https?)://([^/]*)(.+)$")

    -- perform the request
    headers = {}
    local gdata_version = "2"
    headers.Host = host
    headers['GData-Version'] = gdata_version
    headers.Authorization = gdata_auth

    local json = generate_json(view_id, start_date, end_date, self.params.metrics)
    -- mtev.log('debug', 'googleanalytics4 args: %s\n', json)

    client:do_request("POST", uri, headers, json)
    client:get_response()

    -- for line in string.gmatch(output, "([^\r\n]+)") do
    --     mtev.log("debug", "googleanalytics4 response: %s\n", line)
    -- end

    local metrics = 0
    local jsondoc = mtev.parsejson(output)
    local jsondata = jsondoc:document()

    if jsondata['error'] ~= nil then
        local err_code = jsondata['error']['code'] or 'unknown'
        local err_status = jsondata['error']['status'] or 'error'
        local err_message = jsondata['error']['message'] or 'unspecified error'
        mtev.log('error',
            'googleanalytics4 response: code: %s status: %s\nmessage: %s\n',
            err_code, err_status, err_message
        )
        return -1, err_message
    end

    if jsondata['reports'] ~= nil then
        for report_idx, report in pairs(jsondata['reports']) do
            local report = jsondata['reports'][report_idx]

            -- mtev.log('debug', 'googleanalytics4 processing report %d %s\n', report_idx, dump(report))

            local metric_headers = report['columnHeader']['metricHeader']['metricHeaderEntries']
            local metric_values = {}

            if report['data']['totals'] ~= nil then
                metric_values = report['data']['totals'][1]['values']
            elseif report['data']['rows'] ~= nil then
                -- fallback to rows if the report data does not have a totals element
                metric_values = report['data']['rows'][1]['metrics'][1]['values']
            else
                mtev.log('error', 'googleanalytics4 no viable metrics found in data element %s\n', dump(report['data']))
                break
            end

            for offset, metric_header in pairs(metric_headers) do
                local metric_name = string.gsub(metric_header['name'], '^ga:', '')
                local metric_value = metric_values[offset]
                metrics = metrics + 1
                cache_table[metric_name] = metric_value
            end
        end
    end

    if metrics == 0 then
        return -1, 'no metrics found in googleanalytics4 API response'
    end

    return 0

end

return GoogleAnalytics

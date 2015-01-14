-- Copyright (c) 2010-2015, Circonus, Inc.
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

module(..., package.seeall)

function onload(image)
  image.xml_description([=[
<module>
  <name>]=] .. image.name() .. [=[</name>
  <description><para>The googleanalytics module gathers metrics via the Google Analytics export API.</para>
  <para><ulink url="http://code.google.com/apis/analytics/"><citetitle>Google Analytics</citetitle></ulink> provides access to Google Analytics data feeds over a lightweight API.</para>
  <para>This module rides on the http module and provides a secondary phase of XML parsing on the contents that extracts Google Analytics data into metrics that can be trended.</para>
  </description>
  <loader>lua</loader>
  <object>noit.module.googleanalytics</object>
  <moduleconfig>
  <parameter name="api_key"
             required="optional"
             allowed=".+">The API Key used for username/password OAUTH 1.0 based authentication.</parameter>
  <parameter name="client_id"
             required="optional"
             allowed=".+">The Google-Generated client ID used for OAUTH 2.0 authentication.</parameter>
  <parameter name="client_secret"
             required="optional"
             allowed=".+">The Google-Generated client secret used for OAUTH 2.0 authentication.</parameter>
  </moduleconfig>
  <checkconfig>
    <parameter name="username"
               required="optional"
               allowed=".+">The username email address used to login to your Google Analytics account.</parameter>
    <parameter name="password"
               required="optional"
               allowed=".+">The password used to login to your Google Analytics account.</parameter>
    <parameter name="use_oauth"
                required="optional" 
                allowed="^(?:true|false|on|off)$" 
                default="false">Use OAuth authorization instead of username/password.</parameter>
    <parameter name="oauth_token"
               required="required"
               allowed=".+">The OAuth token used to access Google Analytics data.</parameter>
    <parameter name="oauth_token_secret"
               required="required"
               allowed=".+">The OAuth token secret key used to access Google Analytics data.</parameter>
    <parameter name="table_id"
               required="required"
               allowed=".+">The table ID for the data feed to access.</parameter>
    <parameter name="oauth_version"
               required="required"
               allowed=".+">The version of OAuth used.</parameter>
  </checkconfig>
  <examples>
    <example>
      <title>Checking Google Analytics data for Acme, Inc.</title>
      <para>This example checks the Google Analytics data feed for Acme, Inc.</para>
      <programlisting><![CDATA[
      <noit>
        <modules>
          <loader image="lua" name="lua">
            <config><directory>/opt/reconnoiter/libexec/modules-lua/?.lua</directory></config>
          </loader>
          <module loader="lua" name="googleanalytics:m1" object="noit.module.googleanalytics"/>
        </modules>
        <checks>
          <acme module="googleanalytics:m1">
            <check uuid="36b8ba72-7968-11dd-a67f-d39a2cc3f9de"/>
          </acme>
        </checks>
      </noit>
    ]]></programlisting>
    </example>
  </examples>
</module>
]=]);
  return 0
end

local cache_table = { }

-- default values for api key, client id, and client secret
-- these can be overwritten with the config file
local api_key       = ''
local client_id     = ''
local client_secret = ''

function init(module)
  cache_table = { }
  return 0
end

function config(module, options)
 -- api key is only used for non-oauth transactions
 -- or oauth 1.0 transactions... allow setting it, 
 -- though in practice, this will probably never 
 -- actually get set
  if options.api_key ~= nil then
    api_key = options.api_key
  end

  -- need a client id and client secret in order to
  -- use oauth. set defaults, but allow users to configure
  -- their own for inside deployments
  if options.client_id ~= nil then
    client_id = options.client_id
  end
  if options.client_secret ~= nil then
    client_secret = options.client_secret
  end

  return 0
end

local GoogleAnalytics = require 'noit.GoogleAnalytics'

function initiate(module, check)
    local params = {
      username           = check.config.username,
      password           = check.config.password,
      use_oauth          = check.config.use_oauth,
      oauth_token        = check.config.oauth_token,
      oauth_token_secret = check.config.oauth_token_secret,
      table_id           = check.config.table_id,
      oauth_version      = check.config.oauth_version
    }

    local uuid         = check.checkid
    local table_hit    = cache_table[uuid]
    local current_time = os.time()

    if not table_hit then
        local new_hash = {}
        new_hash['timestamp'] = 0;
        new_hash['metrics'] = {};
        cache_table[uuid] = new_hash;
    end

    if module.name() == 'googleanalytics:m2' then
      params.metrics = { 'adClicks', 'adCost', 'adCPC', 'adCPM',
                         'adCTR', 'impressions' }
    elseif module.name() == 'googleanalytics:m3' then
      params.metrics = { 'uniquePageviews' }
    elseif module.name() == 'googleanalytics:m4' then
      params.metrics = { 'itemRevenue', 'itemQuantity', 'transactions',
                         'transactionRevenue', 'transactionShipping',
                         'transactionTax', 'uniquePurchases' }
    elseif module.name() == 'googleanalytics:m5' then
      params.metrics = { 'searchDepth', 'searchDuration', 'searchExits',
                         'searchRefinements', 'searchUniques',
                         'searchVisits' }
    elseif module.name() == 'googleanalytics:m6' then
      params.metrics = { 'goalCompletionsAll', 'goalStartsAll',
                         'goalValueAll' }
    elseif module.name() == 'googleanalytics:m7' then
      params.metrics = { 'totalEvents', 'uniqueEvents', 'eventValue' }
    else
      params.metrics = { 'bounces', 'entrances', 'exits', 'newVisits',
                         'pageviews', 'timeOnPage', 'timeOnSite',
                         'visitors', 'visits' } 
    end

    -- assume the worst.
    check.bad()
    check.unavailable()

    local metric_count = 0
    local check_metric = function (name,value)
        if value == nil then
            check.metric_double(name, value)
        elseif tonumber(value) ~= nil then
            if string.find(value, "%.") ~= nil then
                check.metric_double(name, value)
            else
                check.metric_uint64(name, value)
            end
        else
            check.metric_string(name, value)
        end
        metric_count = metric_count + 1
    end

    -- Check about every 15 minutes
    if current_time - cache_table[uuid]['timestamp'] >= 890 then
      -- We've gone over the cache timeout... get new values
      cache_table[uuid]['metrics'] = {}
      local dns = noit.dns()
      local r = dns:lookup('www.googleapis.com')
      if not r or r.a == nil then
          check.status("failed to resolve www.googleapis.com")
          cache_table[uuid]['timestamp'] = 0
          return
      end

      local ga = GoogleAnalytics:new(params, {})
      local rv, err = ga:perform(r.a, cache_table[uuid]['metrics'], api_key, client_id, client_secret)
      if rv ~= 0 then
        check.status(err or "unknown error")
        cache_table[uuid]['timestamp'] = 0
        return
      end
      cache_table[uuid]['timestamp'] = current_time
    end

    for k,v in pairs(cache_table[uuid]['metrics']) do
      check_metric(k, v)
    end

    check.available()
    if metric_count > 0 then check.good() else check.bad() end
    check.status("metrics=" .. metric_count)
end


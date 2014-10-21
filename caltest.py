#!/usr/bin/env python
import logging
import urllib2
import json
from dateutil import parser, zoneinfo, tz
from datetime import datetime, timedelta

tz_abbr = {
  'ET': 'America/New_York',
  'CT': 'America/Chicago',
  'MT': 'America/Denver',
  'RT': 'America/Phoeniz',
  'PT': 'America/Los_Angeles',
  'AT': 'America/Anchorage',
  'HT': 'Pacific/Honolulu',
}
tzi = dict([(k, zoneinfo.gettz(v)) for k,v in tz_abbr.iteritems()])

def get_schedule(schedule_id):
  url = 'https://spreadsheets.google.com/feeds/list/%s/od6/public/values?alt=json' % schedule_id
  try:
    response = urllib2.urlopen(url)
    content = response.read()
  except urllib2.URLError:
    logging.warning('Warning: could not retrieve spreadsheet data for %s' % schedule_id)
    return False

  try:
    data = json.loads(content)
  except ValueError:
    logging.error('Error: invalid JSON format for spreadsheet %s' % schedule_id)
    return False

  if 'feed' not in data or 'entry' not in data['feed']:
    logging.warning('Warning: invalid data format for %s' % schedule_id)
    return False

  data_keys = [
    ('gsx$day', 'day'),
    ('gsx$time', 'time'),
    ('gsx$temperature', 'temperature'),
  ]

  # Process entries in spreadsheet
  schedule = []
  for entry in data['feed']['entry']:
    result = {}
    for entry_key, result_key in data_keys:
      if entry_key not in entry or '$t' not in entry[entry_key]:
        logging.warning('Warning: key not found for %s: %s' % (schedule_id, entry_key))
        return False
      result[result_key] = entry[entry_key]['$t']

    day_time = '%s %s' % (result['day'], result['time'])
    try:
      result['datetime'] = parser.parse(day_time)
    except ValueError:
      logging.warning('Warning: invalid time format for %s: %s' % (schedule_id, day_time))
      return False
    try:
      result['temperature'] = int(result['temperature'])
    except ValueError:
      logging.warning('Warning: invalid temperature format for %s: %s'
          % (schedule_id, result['temperature']))
      return False

    schedule.append({'d': result['day'], 't': result['time'], 'e': result['temperature']})

  return json.dumps(schedule, separators=(',', ':'))

def normalize(dt_str):
  dt = parser.parse(dt_str, tzinfos=tzi).astimezone(tz.tzutc()).replace(tzinfo=None)

  # Normalize each datetime to within one week from now
  now = datetime.utcnow()
  oneweek = timedelta(days=7)
  oneweek_from_now = now + oneweek
  while dt < now:
    dt += oneweek
  while dt > oneweek_from_now:
    dt -= oneweek

  return dt

def get_next_event(schedule):
  schedule = json.loads(schedule)
  current_schedule = [{
    'dt': parser.parse('%s %s' % (entry['d'], entry['t'])) + timedelta(hours=-17),
    'e': entry['e'],
  } for entry in schedule]

  current_schedule.sort(key=lambda x:x['dt'])
  now = datetime.utcnow()
  print now
  # Rotate list until next entry is first
  while current_schedule[0]['dt'] < now:
    entry = current_schedule.pop(0)
    entry['dt'] += timedelta(days=7)
    current_schedule.append(entry)

  return current_schedule[-1]['e'], current_schedule[0]['dt']


# TODO: Store spreadsheet id, schedule, current set temp, next set temp/time
#
# TODO: Process weekday/weekend
# TODO: handle time zone?
#
# read next_event from Datastore
# if next_event is None or < now
#   calculate last_event and next_event
#   set_temp = last_event.temp
#   store next_event

# schedule = get_schedule('1kGVn93ceoVzeOcern7KuixuDSfPJFJNN_gIDGM7Bc_g')
# if not schedule:
#   print 'Failed'
# print 'Schedule: ' + schedule
# print get_next_event(schedule)

print normalize('Monday 10pm PT')

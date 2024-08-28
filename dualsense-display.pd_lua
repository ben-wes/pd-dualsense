local ds = pd.Class:new():register('dualsense-display')
local shapes = require('dualsense-shapes')

function ds:initialize(sel, atoms)
    self.inlets = 1
    self.size = {619, 384}
    self.scale = 1
    self:set_size(self.size[1], self.size[2])
    self.delay_time = 33.333333
    self.time = 0
    self.touch_radius = 20
    self.outlines = false
    self.color_active = {255, 77, 100}
    -- self.color_active = {100, 180, 77}
    self.color_cover = {223, 227, 234}
    -- self.color_cover = {20, 20, 20}
    self.color_button = {255, 255, 255}
    self.color_inlay = {0, 0, 0}
    -- self.color_led_dark = {100, 10, 30}
    -- self.color_led_bright = {180, 10, 30}
    self.color_led_dark = {35, 50, 100}
    self.color_led_bright = {35, 70, 180}
    self.color_case = {35, 37, 38}
    self.normalized_accel = {0, 1, 0}

    self.state = {
        button_dir_left = 0,
        button_dir_up = 0,
        button_dir_right = 0,
        button_dir_down = 0,
        button_action_square = 0,
        button_action_cross = 0,
        button_action_circle = 0,
        button_action_triangle = 0,
        button_mute = 0,
        button_options = 0,
        button_create = 0,
        button_ps = 0,
        button_stick_l = 0,
        button_stick_r = 0,
        button_l1 = 0,
        button_r1 = 0,
        button_l2 = 0,
        button_r2 = 0,
        button_pad = 0,
        pad1_touch = 0,
        pad1_x = 0,
        pad1_y = 0,
        pad2_touch = 0,
        pad2_x = 0,
        pad2_y = 0,
        trigger_l = 0,
        trigger_r = 0,
        stick_l_x = 0,
        stick_l_y = 0,
        stick_r_x = 0,
        stick_r_y = 0,
        gyro_x = 0,
        gyro_y = 0,
        gyro_z = 0,
        accel_x = 0,
        accel_y = 0,
        accel_z = 0
    }
    return true
end

function ds:postinitialize()
    self.clock = pd.Clock:new():register(self, "tick")
    self.clock:delay(self.delay_time)
end

function ds:finalize()
    self.clock:destruct()
end

function ds:blend_color(col1, col2, t)
    t = math.max(math.min(t, 1), 0) -- clip 0..1
    local color = {0, 0, 0}
    for i = 1, 3 do
        color[i] = (1 - t) * col1[i] + t * col2[i]
    end
    return color
end

function ds:state_color(input, fg, bg)
    fg = fg or self.color_active
    bg = bg or self.color_button
    return table.unpack(self:blend_color(bg, fg, self.state[input]))
end

function ds:in_1_dpad(atoms)
    if atoms[1] == 'x' then
        self.state.button_dir_left = math.max(atoms[2] * -1)
        self.state.button_dir_right = math.max(atoms[2])
    elseif atoms[1] == 'y' then
        self.state.button_dir_down = math.max(atoms[2] * -1)
        self.state.button_dir_up = math.max(atoms[2])
    end
end

function ds:in_1_gyro(atoms)
    if atoms[1] == 'x' then self.state.gyro_x = atoms[2] end
    if atoms[1] == 'y' then self.state.gyro_y = atoms[2] end
    if atoms[1] == 'z' then self.state.gyro_z = atoms[2] end
end

function ds:in_1_accel(atoms)
    if atoms[1] == 'x' then self.state.accel_x = atoms[2] end
    if atoms[1] == 'y' then self.state.accel_y = atoms[2] end
    if atoms[1] == 'z' then self.state.accel_z = atoms[2] end
end

function ds:in_1_button(atoms)
    if atoms[1] == 'circle' then self.state.button_action_circle = atoms[2] end
    if atoms[1] == 'square' then self.state.button_action_square = atoms[2] end
    if atoms[1] == 'triangle' then self.state.button_action_triangle = atoms[2] end
    if atoms[1] == 'cross' then self.state.button_action_cross = atoms[2] end

    if atoms[1] == 'mute' then self.state.button_mute = atoms[2] end
    if atoms[1] == 'options' then self.state.button_options = atoms[2] end
    if atoms[1] == 'create' then self.state.button_create = atoms[2] end
    if atoms[1] == 'ps' then self.state.button_ps = atoms[2] end

    if atoms[1] == 'l1' then self.state.button_l1 = atoms[2] end
    if atoms[1] == 'r1' then self.state.button_r1 = atoms[2] end
    if atoms[1] == 'l2' then self.state.button_l2 = atoms[2] end
    if atoms[1] == 'r2' then self.state.button_r2 = atoms[2] end

    if atoms[1] == 'stick_l' then self.state.button_stick_l = atoms[2] end
    if atoms[1] == 'stick_r' then self.state.button_stick_r = atoms[2] end

    if atoms[1] == 'pad' then self.state.button_pad = atoms[2] end
end

function ds:in_1_trigger(atoms)
    if atoms[1] == 'l' then
        self.state.trigger_l = atoms[2] * 3
    elseif atoms[1] == 'r' then
        self.state.trigger_r = atoms[2] * 3
    end
end

function ds:in_1_stick(atoms)
    if atoms[1] == 'l' then
        if atoms[2] == 'x' then
            self.state.stick_l_x = atoms[3] * 30
        elseif atoms[2] == 'y' then
            self.state.stick_l_y = atoms[3] * 30
        end
    elseif atoms[1] == 'r' then
        if atoms[2] == 'x' then
            self.state.stick_r_x = atoms[3] * 30
        elseif atoms[2] == 'y' then
            self.state.stick_r_y = atoms[3] * 30
        end
    end
end

function ds:in_1_pad(atoms)
    if atoms[1] == 'touch_0' then
        if atoms[2] == 'x' then
            local touched = atoms[3] >= 0
            self.state.pad1_touch = touched and 1.2 or 0
            if touched then self.state.pad1_x = atoms[3] end
        elseif atoms[2] == 'y' then
            local touched = atoms[3] >= 0
            self.state.pad1_touch = touched and 1.2 or 0
            if touched then self.state.pad1_y = atoms[3] end
        end
    elseif atoms[1] == 'touch_1' then
        if atoms[2] == 'x' then
            local touched = atoms[3] >= 0
            self.state.pad2_touch = touched and 1.2 or 0
            if touched then self.state.pad2_x = atoms[3] end
        elseif atoms[2] == 'y' then
            local touched = atoms[3] >= 0
            self.state.pad2_touch = touched and 1.2 or 0
            if touched then self.state.pad2_y = atoms[3] end
        end
    end
end

function ds:in_1_scale(x)
    self.scale = x[1]
    self:set_size(self.size[1]*self.scale, self.size[2]*self.scale)
end

function ds:paint(g)
    g:scale(self.scale, self.scale)
    g:translate(22, 6)

    g:set_color(table.unpack(self.color_case)); g:fill_path(shapes.background())
    g:set_color(table.unpack(self:blend_color(self.color_led_dark, self.color_led_bright, math.sin(self.time) * 0.5 + 0.5)))
    g:fill_path(shapes.led())

    g:translate(0, self.state.button_l1*3)
    g:set_color(self:state_color('button_l1', self.color_active, self.color_case)); g:fill_path(shapes.button_l1())
    g:translate(0, self.state.button_r1*3-self.state.button_l1*3)
    g:set_color(self:state_color('button_r1', self.color_active, self.color_case)); g:fill_path(shapes.button_r1())
    g:translate(0, -self.state.button_r1*3)

    g:translate(48, 28)
    g:set_color(self:state_color('button_l2', self.color_active, self.color_case)); g:fill_path(shapes.button_l2(-self.state.trigger_l*0.28-0.3))
    g:translate(480, 0)
    g:set_color(self:state_color('button_r2', self.color_active, self.color_case)); g:fill_path(shapes.button_r2(self.state.trigger_r*0.28+0.3))
    g:translate(-528, -28)

    g:set_color(table.unpack(self.color_cover)); for _, shape in ipairs(shapes.cover()) do g:fill_path(shape) end
    g:set_color(table.unpack(self.color_inlay)); for _, shape in ipairs(shapes.inlays()) do g:fill_path(shape) end
    g:set_color(table.unpack(self.color_inlay)); g:fill_path(shapes.inlay_pd())

    g:set_color(self:state_color('button_pad', self.color_active, self.color_cover)); g:fill_path(shapes.pad())
    g:set_color(self:state_color('button_mute')); g:fill_path(shapes.button_mute())
    g:set_color(self:state_color('button_options')); g:fill_path(shapes.button_options())
    g:set_color(self:state_color('button_create')); g:fill_path(shapes.button_create())
    -- g:set_color(self:state_color('button_ps', self.color_active, self.color_case)); for _, shape in ipairs(shapes.button_ps()) do g:fill_path(shape) end

    g:set_color(self:state_color('button_ps', self.color_active, self.color_case)); for _, shape in ipairs(shapes.button_pd()) do g:fill_path(shape) end
    g:set_color(table.unpack(self.color_inlay)); for _, shape in ipairs(shapes.button_pd_content()) do g:fill_path(shape) end

    g:set_color(self:state_color('button_dir_left')); g:fill_path(shapes.button_dir_left())
    g:set_color(table.unpack(self.color_inlay)); g:fill_path(shapes.button_dir_left_content())

    g:set_color(self:state_color('button_dir_up')); g:fill_path(shapes.button_dir_up())
    g:set_color(table.unpack(self.color_inlay)); g:fill_path(shapes.button_dir_up_content())

    g:set_color(self:state_color('button_dir_right')); g:fill_path(shapes.button_dir_right())
    g:set_color(table.unpack(self.color_inlay)); g:fill_path(shapes.button_dir_right_content())

    g:set_color(self:state_color('button_dir_down')); g:fill_path(shapes.button_dir_down())
    g:set_color(table.unpack(self.color_inlay)); g:fill_path(shapes.button_dir_down_content())

    g:set_color(self:state_color('button_action_square')); g:fill_path(shapes.button_action_square())
    g:set_color(table.unpack(self.color_inlay)); g:stroke_rect(415.7, 96.7, 20, 20, 1)

    g:set_color(self:state_color('button_action_cross')); g:fill_path(shapes.button_action_cross())
    g:set_color(table.unpack(self.color_inlay)); g:draw_line(458, 139, 476, 157, 1); g:draw_line(458, 157, 476, 139, 1)

    g:set_color(self:state_color('button_action_triangle')); g:fill_path(shapes.button_action_triangle())
    g:set_color(table.unpack(self.color_inlay)); g:stroke_path(shapes.button_action_triangle_content(), 1)

    g:set_color(self:state_color('button_action_circle')); g:fill_path(shapes.button_action_circle())
    g:set_color(table.unpack(self.color_inlay)); g:stroke_ellipse(497, 94, 23.8, 23.8, 1)

    g:translate(self.state.stick_l_x, -self.state.stick_l_y)
    g:set_color(self:state_color('button_stick_l')); g:fill_path(shapes.stick_l())
    g:set_color(table.unpack(self.color_inlay)); g:fill_path(shapes.stick_l_content())
    g:translate(-self.state.stick_l_x+self.state.stick_r_x, self.state.stick_l_y-self.state.stick_r_y)
    g:set_color(self:state_color('button_stick_r')); g:fill_path(shapes.stick_r())
    g:set_color(table.unpack(self.color_inlay)); g:fill_path(shapes.stick_r_content())
    g:translate(-self.state.stick_r_x, self.state.stick_r_y)

    -- local lookat_center = {289, 63}
    -- local lookat_radius = 20

    -- local l = 1 -- /math.sqrt(3)
    -- local points = {
    --     {l, l, -l},
    --     {-l, l, -l},
    --     {-l, -l, -l},
    --     {l, -l, -l},
    --     {l, l, l},
    --     {-l, l, l},
    --     {-l, -l, l},
    --     {l, -l, l}
    -- }

    -- local connections = {
    --     {5, 6}, {6, 7}, {7, 8}, {8, 5}, -- top
    --     {1, 2}, {2, 3}, {3, 4}, {4, 1}, -- bottom
    --     {1, 5}, {2, 6}, {3, 7}, {4, 8}  -- connecting bottom w/ top
    -- }

    -- for i, point in ipairs(points) do
    --     points[i] = shapes.pitchRollTo(point, lookat)
    -- end

    -- g:set_color(255, 255, 255)
    -- for i, connection in ipairs(connections) do
    --     local from = points[connection[1]]
    --     local to = points[connection[2]]
    --     local scale_from = 5 / (5 - from[2]) * lookat_radius
    --     local scale_to = 5 / (5 - to[2]) * lookat_radius
    --     local strokewidth = 1
    --     if i<=4 then strokewidth = 3 end
    --     g:draw_line(
    --         -from[1] * scale_from + lookat_center[1],
    --         -from[3] * scale_from + lookat_center[2],
    --         -to[1] * scale_to + lookat_center[1],
    --         -to[3] * scale_to + lookat_center[2], strokewidth)
    -- end

    if self.state.pad1_touch > 0 then
        g:set_color(self:state_color('pad1_touch', self.color_active, self.color_cover))
        g:fill_ellipse(self.state.pad1_x * 180 + 190, self.state.pad1_y * 100 + 10, self.touch_radius, self.touch_radius)
        g:set_color(self:state_color('pad1_touch', self.color_button, self.color_cover))
        g:draw_text("1", self.state.pad1_x * 180 + 197, self.state.pad1_y * 100 + 14, 10, 12)
    end

    if self.state.pad2_touch > 0 then
        g:set_color(self:state_color('pad2_touch', self.color_active, self.color_cover))
        g:fill_ellipse(self.state.pad2_x * 180 + 190, self.state.pad2_y * 100 + 10, self.touch_radius, self.touch_radius)
        g:set_color(self:state_color('pad2_touch', self.color_button, self.color_cover))
        g:draw_text("2", self.state.pad2_x * 180 + 197, self.state.pad2_y * 100 + 14, 10, 12)
    end

    -- g:reset_transform()
end

function ds:tick()
    self.state.pad1_touch = self.state.pad1_touch - 2 / self.delay_time
    self.state.pad2_touch = self.state.pad2_touch - 2 / self.delay_time
    self.time = self.time + 1 / self.delay_time

    self:repaint()
    self.clock:delay(self.delay_time)
end

function ds:in_1_reload()
   self:dofilex(self._scriptname)
end

function ds:in_1_bang()
    self:repaint()
end

namespace ASCOM.BMDome1
{
    partial class SetupDialogForm
    {
        /// <summary>
        /// Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        /// Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Windows Form Designer generated code

        /// <summary>
        /// Required method for Designer support - do not modify
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            this.cmdOK = new System.Windows.Forms.Button();
            this.cmdCancel = new System.Windows.Forms.Button();
            this.picASCOM = new System.Windows.Forms.PictureBox();
            this.groupBox1 = new System.Windows.Forms.GroupBox();
            this.label9 = new System.Windows.Forms.Label();
            this.TxtTopicBase = new System.Windows.Forms.MaskedTextBox();
            this.label8 = new System.Windows.Forms.Label();
            this.TxtIdCliente = new System.Windows.Forms.MaskedTextBox();
            this.label7 = new System.Windows.Forms.Label();
            this.TxtPassword = new System.Windows.Forms.MaskedTextBox();
            this.TxtUsuario = new System.Windows.Forms.MaskedTextBox();
            this.TxtPuerto = new System.Windows.Forms.MaskedTextBox();
            this.TxtServidor = new System.Windows.Forms.MaskedTextBox();
            this.label5 = new System.Windows.Forms.Label();
            this.label4 = new System.Windows.Forms.Label();
            this.label3 = new System.Windows.Forms.Label();
            this.label2 = new System.Windows.Forms.Label();
            this.label1 = new System.Windows.Forms.Label();
            this.CmdPruebaCom = new System.Windows.Forms.Button();
            this.label6 = new System.Windows.Forms.Label();
            this.picBM = new System.Windows.Forms.PictureBox();
            this.picM31 = new System.Windows.Forms.PictureBox();
            this.comboQoS = new System.Windows.Forms.ComboBox();
            ((System.ComponentModel.ISupportInitialize)(this.picASCOM)).BeginInit();
            this.groupBox1.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)(this.picBM)).BeginInit();
            ((System.ComponentModel.ISupportInitialize)(this.picM31)).BeginInit();
            this.SuspendLayout();
            // 
            // cmdOK
            // 
            this.cmdOK.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Bottom | System.Windows.Forms.AnchorStyles.Right)));
            this.cmdOK.DialogResult = System.Windows.Forms.DialogResult.OK;
            this.cmdOK.Location = new System.Drawing.Point(426, 311);
            this.cmdOK.Name = "cmdOK";
            this.cmdOK.Size = new System.Drawing.Size(71, 24);
            this.cmdOK.TabIndex = 9;
            this.cmdOK.TabStop = false;
            this.cmdOK.Text = "OK";
            this.cmdOK.UseVisualStyleBackColor = true;
            // 
            // cmdCancel
            // 
            this.cmdCancel.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Bottom | System.Windows.Forms.AnchorStyles.Right)));
            this.cmdCancel.DialogResult = System.Windows.Forms.DialogResult.Cancel;
            this.cmdCancel.Location = new System.Drawing.Point(426, 341);
            this.cmdCancel.Name = "cmdCancel";
            this.cmdCancel.Size = new System.Drawing.Size(71, 25);
            this.cmdCancel.TabIndex = 10;
            this.cmdCancel.TabStop = false;
            this.cmdCancel.Text = "Cancel";
            this.cmdCancel.UseVisualStyleBackColor = true;
            // 
            // picASCOM
            // 
            this.picASCOM.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Right)));
            this.picASCOM.Cursor = System.Windows.Forms.Cursors.Hand;
            this.picASCOM.Image = global::ASCOM.BMDome1.Properties.Resources.ASCOM;
            this.picASCOM.Location = new System.Drawing.Point(372, 308);
            this.picASCOM.Name = "picASCOM";
            this.picASCOM.Size = new System.Drawing.Size(48, 56);
            this.picASCOM.SizeMode = System.Windows.Forms.PictureBoxSizeMode.AutoSize;
            this.picASCOM.TabIndex = 3;
            this.picASCOM.TabStop = false;
            this.picASCOM.Click += new System.EventHandler(this.BrowseToAscom);
            this.picASCOM.DoubleClick += new System.EventHandler(this.BrowseToAscom);
            // 
            // groupBox1
            // 
            this.groupBox1.Controls.Add(this.comboQoS);
            this.groupBox1.Controls.Add(this.label9);
            this.groupBox1.Controls.Add(this.TxtTopicBase);
            this.groupBox1.Controls.Add(this.label8);
            this.groupBox1.Controls.Add(this.TxtIdCliente);
            this.groupBox1.Controls.Add(this.label7);
            this.groupBox1.Controls.Add(this.TxtPassword);
            this.groupBox1.Controls.Add(this.TxtUsuario);
            this.groupBox1.Controls.Add(this.TxtPuerto);
            this.groupBox1.Controls.Add(this.TxtServidor);
            this.groupBox1.Controls.Add(this.label5);
            this.groupBox1.Controls.Add(this.label4);
            this.groupBox1.Controls.Add(this.label3);
            this.groupBox1.Controls.Add(this.label2);
            this.groupBox1.Font = new System.Drawing.Font("Microsoft Sans Serif", 14.25F, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.groupBox1.Location = new System.Drawing.Point(18, 17);
            this.groupBox1.Name = "groupBox1";
            this.groupBox1.Size = new System.Drawing.Size(341, 288);
            this.groupBox1.TabIndex = 0;
            this.groupBox1.TabStop = false;
            this.groupBox1.Text = "Servidor MQTT";
            // 
            // label9
            // 
            this.label9.Location = new System.Drawing.Point(96, 251);
            this.label9.Name = "label9";
            this.label9.Size = new System.Drawing.Size(57, 25);
            this.label9.TabIndex = 22;
            this.label9.Text = "QoS:";
            // 
            // TxtTopicBase
            // 
            this.TxtTopicBase.Location = new System.Drawing.Point(155, 211);
            this.TxtTopicBase.Name = "TxtTopicBase";
            this.TxtTopicBase.Size = new System.Drawing.Size(149, 29);
            this.TxtTopicBase.TabIndex = 6;
            this.TxtTopicBase.Text = "Domo1";
            // 
            // label8
            // 
            this.label8.Location = new System.Drawing.Point(32, 213);
            this.label8.Name = "label8";
            this.label8.Size = new System.Drawing.Size(123, 25);
            this.label8.TabIndex = 20;
            this.label8.Text = "Topic Base:";
            // 
            // TxtIdCliente
            // 
            this.TxtIdCliente.Location = new System.Drawing.Point(155, 36);
            this.TxtIdCliente.Name = "TxtIdCliente";
            this.TxtIdCliente.Size = new System.Drawing.Size(149, 29);
            this.TxtIdCliente.TabIndex = 1;
            this.TxtIdCliente.Text = "Domo1";
            // 
            // label7
            // 
            this.label7.Location = new System.Drawing.Point(46, 38);
            this.label7.Name = "label7";
            this.label7.Size = new System.Drawing.Size(107, 25);
            this.label7.TabIndex = 18;
            this.label7.Text = "ID Cliente:";
            // 
            // TxtPassword
            // 
            this.TxtPassword.Location = new System.Drawing.Point(155, 176);
            this.TxtPassword.Name = "TxtPassword";
            this.TxtPassword.PasswordChar = '*';
            this.TxtPassword.Size = new System.Drawing.Size(149, 29);
            this.TxtPassword.TabIndex = 5;
            // 
            // TxtUsuario
            // 
            this.TxtUsuario.Location = new System.Drawing.Point(155, 141);
            this.TxtUsuario.Name = "TxtUsuario";
            this.TxtUsuario.Size = new System.Drawing.Size(149, 29);
            this.TxtUsuario.TabIndex = 4;
            // 
            // TxtPuerto
            // 
            this.TxtPuerto.Location = new System.Drawing.Point(155, 106);
            this.TxtPuerto.Name = "TxtPuerto";
            this.TxtPuerto.Size = new System.Drawing.Size(149, 29);
            this.TxtPuerto.TabIndex = 3;
            this.TxtPuerto.Text = "1883";
            // 
            // TxtServidor
            // 
            this.TxtServidor.Location = new System.Drawing.Point(155, 71);
            this.TxtServidor.Name = "TxtServidor";
            this.TxtServidor.Size = new System.Drawing.Size(149, 29);
            this.TxtServidor.TabIndex = 2;
            // 
            // label5
            // 
            this.label5.Location = new System.Drawing.Point(30, 177);
            this.label5.Name = "label5";
            this.label5.Size = new System.Drawing.Size(137, 25);
            this.label5.TabIndex = 13;
            this.label5.Text = "Contraseña:";
            // 
            // label4
            // 
            this.label4.Location = new System.Drawing.Point(65, 143);
            this.label4.Name = "label4";
            this.label4.Size = new System.Drawing.Size(97, 25);
            this.label4.TabIndex = 12;
            this.label4.Text = "Usuario:";
            // 
            // label3
            // 
            this.label3.Location = new System.Drawing.Point(75, 108);
            this.label3.Name = "label3";
            this.label3.Size = new System.Drawing.Size(97, 25);
            this.label3.TabIndex = 11;
            this.label3.Text = "Puerto:";
            // 
            // label2
            // 
            this.label2.Location = new System.Drawing.Point(58, 73);
            this.label2.Name = "label2";
            this.label2.Size = new System.Drawing.Size(95, 25);
            this.label2.TabIndex = 10;
            this.label2.Text = "Servidor:";
            // 
            // label1
            // 
            this.label1.AutoSize = true;
            this.label1.Location = new System.Drawing.Point(197, 324);
            this.label1.Name = "label1";
            this.label1.Size = new System.Drawing.Size(153, 13);
            this.label1.TabIndex = 20;
            this.label1.Text = "Driver BMDomo1 Version 1.0.0";
            // 
            // CmdPruebaCom
            // 
            this.CmdPruebaCom.Location = new System.Drawing.Point(12, 329);
            this.CmdPruebaCom.Name = "CmdPruebaCom";
            this.CmdPruebaCom.Size = new System.Drawing.Size(159, 23);
            this.CmdPruebaCom.TabIndex = 8;
            this.CmdPruebaCom.Text = "Probar Comunicacion";
            this.CmdPruebaCom.UseVisualStyleBackColor = true;
            // 
            // label6
            // 
            this.label6.AutoSize = true;
            this.label6.Location = new System.Drawing.Point(223, 344);
            this.label6.Name = "label6";
            this.label6.Size = new System.Drawing.Size(97, 13);
            this.label6.TabIndex = 21;
            this.label6.Text = "Bilbaomakers 2019";
            // 
            // picBM
            // 
            this.picBM.Image = global::ASCOM.BMDome1.Properties.Resources.BMLogoORG_200x230;
            this.picBM.Location = new System.Drawing.Point(383, 29);
            this.picBM.Name = "picBM";
            this.picBM.Size = new System.Drawing.Size(103, 120);
            this.picBM.SizeMode = System.Windows.Forms.PictureBoxSizeMode.StretchImage;
            this.picBM.TabIndex = 13;
            this.picBM.TabStop = false;
            // 
            // picM31
            // 
            this.picM31.Image = global::ASCOM.BMDome1.Properties.Resources.M31;
            this.picM31.Location = new System.Drawing.Point(383, 172);
            this.picM31.Name = "picM31";
            this.picM31.Size = new System.Drawing.Size(103, 121);
            this.picM31.SizeMode = System.Windows.Forms.PictureBoxSizeMode.StretchImage;
            this.picM31.TabIndex = 14;
            this.picM31.TabStop = false;
            // 
            // comboQoS
            // 
            this.comboQoS.DropDownStyle = System.Windows.Forms.ComboBoxStyle.DropDownList;
            this.comboQoS.FormattingEnabled = true;
            this.comboQoS.Items.AddRange(new object[] {
            "0",
            "1",
            "2"});
            this.comboQoS.Location = new System.Drawing.Point(155, 247);
            this.comboQoS.MaxDropDownItems = 3;
            this.comboQoS.MaxLength = 1;
            this.comboQoS.Name = "comboQoS";
            this.comboQoS.Size = new System.Drawing.Size(149, 32);
            this.comboQoS.TabIndex = 7;
            // 
            // SetupDialogForm
            // 
            this.AcceptButton = this.cmdOK;
            this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.CancelButton = this.cmdCancel;
            this.ClientSize = new System.Drawing.Size(509, 379);
            this.Controls.Add(this.picM31);
            this.Controls.Add(this.picBM);
            this.Controls.Add(this.label6);
            this.Controls.Add(this.CmdPruebaCom);
            this.Controls.Add(this.label1);
            this.Controls.Add(this.groupBox1);
            this.Controls.Add(this.picASCOM);
            this.Controls.Add(this.cmdCancel);
            this.Controls.Add(this.cmdOK);
            this.FormBorderStyle = System.Windows.Forms.FormBorderStyle.FixedDialog;
            this.MaximizeBox = false;
            this.MinimizeBox = false;
            this.Name = "SetupDialogForm";
            this.SizeGripStyle = System.Windows.Forms.SizeGripStyle.Hide;
            this.StartPosition = System.Windows.Forms.FormStartPosition.CenterScreen;
            this.Text = "Configuracion BMDome1";
            ((System.ComponentModel.ISupportInitialize)(this.picASCOM)).EndInit();
            this.groupBox1.ResumeLayout(false);
            this.groupBox1.PerformLayout();
            ((System.ComponentModel.ISupportInitialize)(this.picBM)).EndInit();
            ((System.ComponentModel.ISupportInitialize)(this.picM31)).EndInit();
            this.ResumeLayout(false);
            this.PerformLayout();

        }

        #endregion

        private System.Windows.Forms.Button cmdOK;
        private System.Windows.Forms.Button cmdCancel;
        private System.Windows.Forms.PictureBox picASCOM;
        private System.Windows.Forms.GroupBox groupBox1;
        private System.Windows.Forms.MaskedTextBox TxtServidor;
        private System.Windows.Forms.Label label5;
        private System.Windows.Forms.Label label4;
        private System.Windows.Forms.Label label3;
        private System.Windows.Forms.Label label2;
        private System.Windows.Forms.MaskedTextBox TxtPuerto;
        private System.Windows.Forms.Label label1;
        private System.Windows.Forms.Button CmdPruebaCom;
        private System.Windows.Forms.Label label6;
        private System.Windows.Forms.MaskedTextBox TxtPassword;
        private System.Windows.Forms.MaskedTextBox TxtUsuario;
        private System.Windows.Forms.MaskedTextBox TxtTopicBase;
        private System.Windows.Forms.Label label8;
        private System.Windows.Forms.MaskedTextBox TxtIdCliente;
        private System.Windows.Forms.Label label7;
        private System.Windows.Forms.Label label9;
        private System.Windows.Forms.PictureBox picBM;
        private System.Windows.Forms.PictureBox picM31;
        private System.Windows.Forms.ComboBox comboQoS;
    }
}